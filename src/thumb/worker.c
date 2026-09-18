#include "thumb/worker.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "thumb/cache.h"
#include "thumb/decode.h"
#include "thumb/scale.h"

#define MAX_WORKERS 4

struct job {
	struct matuwall_thumb_result result;
	const char *path;
	uint32_t target_w;
	uint32_t target_h;
	struct job *next;
};

struct job_list {
	struct job *head;
	struct job *tail;
};

struct matuwall_worker_pool {
	pthread_t threads[MAX_WORKERS];
	size_t thread_count;

	uint32_t target_w;
	uint32_t target_h;

	pthread_mutex_t mutex;
	pthread_cond_t wakeup;
	struct job *jobs_head;
	struct job *jobs_tail;
	struct job *results;
	bool stopping;

	int event_fd;
	struct matuwall_cache *cache;
};

static size_t worker_count(void) {
	long online = sysconf(_SC_NPROCESSORS_ONLN);
	if (online < 1) {
		online = 1;
	}
	return online < MAX_WORKERS ? (size_t)online : MAX_WORKERS;
}

static bool produce(const struct matuwall_worker_pool *pool,
	const struct job *job, struct matuwall_image *out, bool *cache_hit) {
	*cache_hit = false;
	// Output-sized previews would bloat the cache for one keypress of value
	bool thumbnail = job->result.kind == MATUWALL_JOB_THUMB;

	char key[640];
	bool have_key = thumbnail && pool->cache != NULL &&
			matuwall_cache_key(pool->cache, job->path,
				job->target_w, job->target_h, key, sizeof(key));
	if (have_key && matuwall_cache_read(key, out)) {
		*cache_hit = true;
		return true;
	}

	struct matuwall_image decoded;
	enum matuwall_decode_purpose purpose =
		thumbnail ? MATUWALL_DECODE_THUMBNAIL : MATUWALL_DECODE_PREVIEW;
	if (!matuwall_image_decode(&decoded, job->path, job->target_w,
		    job->target_h, purpose)) {
		return false;
	}
	bool scaled = true;
	if (decoded.width == job->target_w && decoded.height == job->target_h) {
		*out = decoded;
	} else {
		scaled = matuwall_scale_cover(
			&decoded, job->target_w, job->target_h, out);
		matuwall_image_free(&decoded);
	}
	if (!scaled) {
		return false;
	}

	if (have_key) {
		matuwall_cache_write(key, out);
	}
	return true;
}

static struct job *take_job(struct matuwall_worker_pool *pool) {
	while (pool->jobs_head == NULL && !pool->stopping) {
		pthread_cond_wait(&pool->wakeup, &pool->mutex);
	}
	if (pool->stopping) {
		return NULL;
	}
	struct job *job = pool->jobs_head;
	pool->jobs_head = job->next;
	if (pool->jobs_head == NULL) {
		pool->jobs_tail = NULL;
	}
	return job;
}

static void publish_result(struct matuwall_worker_pool *pool, struct job *job) {
	pthread_mutex_lock(&pool->mutex);
	job->next = pool->results;
	pool->results = job;
	pthread_mutex_unlock(&pool->mutex);

	uint64_t one = 1;
	ssize_t written = write(pool->event_fd, &one, sizeof(one));
	(void)written;
}

static void *worker_main(void *arg) {
	struct matuwall_worker_pool *pool = arg;

	pthread_mutex_lock(&pool->mutex);
	for (;;) {
		struct job *job = take_job(pool);
		if (job == NULL) {
			break;
		}
		pthread_mutex_unlock(&pool->mutex);

		struct matuwall_image img;
		if (produce(pool, job, &img, &job->result.cache_hit)) {
			job->result.ok = true;
			job->result.pixels = img.pixels;
			job->result.width = img.width;
			job->result.height = img.height;
		}

		publish_result(pool, job);
		pthread_mutex_lock(&pool->mutex);
	}
	pthread_mutex_unlock(&pool->mutex);
	return NULL;
}

struct matuwall_worker_pool *matuwall_worker_pool_start(
	uint32_t target_w, uint32_t target_h) {
	struct matuwall_worker_pool *pool = calloc(1, sizeof(*pool));
	if (pool == NULL) {
		return NULL;
	}
	pool->target_w = target_w;
	pool->target_h = target_h;
	pthread_mutex_init(&pool->mutex, NULL);
	pthread_cond_init(&pool->wakeup, NULL);

	pool->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (pool->event_fd < 0) {
		pthread_cond_destroy(&pool->wakeup);
		pthread_mutex_destroy(&pool->mutex);
		free(pool);
		return NULL;
	}
	pool->cache = matuwall_cache_create();

	size_t want = worker_count();
	for (size_t i = 0; i < want; i++) {
		if (pthread_create(
			    &pool->threads[i], NULL, worker_main, pool) != 0) {
			break;
		}
		pool->thread_count++;
	}
	if (pool->thread_count == 0) {
		matuwall_worker_pool_stop(pool);
		return NULL;
	}
	return pool;
}

int matuwall_worker_pool_fd(const struct matuwall_worker_pool *pool) {
	return pool->event_fd;
}

static struct job *make_job(enum matuwall_job_kind kind, size_t index,
	const char *path, uint32_t target_w, uint32_t target_h) {
	struct job *job = calloc(1, sizeof(*job));
	if (job == NULL) {
		return NULL;
	}
	job->result.kind = kind;
	job->result.index = index;
	job->target_w = target_w;
	job->target_h = target_h;
	job->path = path;
	return job;
}

static void job_list_append(struct job_list *list, struct job *job) {
	job->next = NULL;
	if (list->tail != NULL) {
		list->tail->next = job;
	} else {
		list->head = job;
	}
	list->tail = job;
}

static void job_list_extend(struct job_list *list, struct job_list *addition) {
	if (addition->head == NULL) {
		return;
	}
	if (list->tail != NULL) {
		list->tail->next = addition->head;
	} else {
		list->head = addition->head;
	}
	list->tail = addition->tail;
}

// Caller holds the mutex
static void drop_queued_previews(struct matuwall_worker_pool *pool) {
	struct job **cursor = &pool->jobs_head;
	struct job *prev = NULL;

	while (*cursor != NULL) {
		struct job *job = *cursor;
		if (job->result.kind != MATUWALL_JOB_PREVIEW) {
			prev = job;
			cursor = &job->next;
			continue;
		}
		*cursor = job->next;
		if (pool->jobs_tail == job) {
			pool->jobs_tail = prev;
		}
		free(job);
	}
}

bool matuwall_worker_submit(
	struct matuwall_worker_pool *pool, size_t index, const char *path) {
	struct job *job = make_job(MATUWALL_JOB_THUMB, index, path,
		pool->target_w, pool->target_h);
	if (job == NULL) {
		return false;
	}

	pthread_mutex_lock(&pool->mutex);
	if (pool->jobs_tail != NULL) {
		pool->jobs_tail->next = job;
	} else {
		pool->jobs_head = job;
	}
	pool->jobs_tail = job;
	pthread_cond_signal(&pool->wakeup);
	pthread_mutex_unlock(&pool->mutex);
	return true;
}

void matuwall_worker_prioritize_thumbs(struct matuwall_worker_pool *pool,
	size_t first, size_t end, size_t wrap_end) {
	if (first >= end && wrap_end == 0) {
		return;
	}

	struct job_list previews = {0};
	struct job_list visible = {0};
	struct job_list remaining = {0};

	pthread_mutex_lock(&pool->mutex);
	struct job *job = pool->jobs_head;
	while (job != NULL) {
		struct job *next = job->next;
		if (job->result.kind == MATUWALL_JOB_PREVIEW) {
			job_list_append(&previews, job);
		} else if ((job->result.index >= first &&
				   job->result.index < end) ||
			   job->result.index < wrap_end) {
			job_list_append(&visible, job);
		} else {
			job_list_append(&remaining, job);
		}
		job = next;
	}

	job_list_extend(&previews, &visible);
	job_list_extend(&previews, &remaining);
	pool->jobs_head = previews.head;
	pool->jobs_tail = previews.tail;
	pthread_mutex_unlock(&pool->mutex);
}

bool matuwall_worker_submit_preview(struct matuwall_worker_pool *pool,
	size_t index, const char *path, uint32_t target_w, uint32_t target_h) {
	struct job *job =
		make_job(MATUWALL_JOB_PREVIEW, index, path, target_w, target_h);
	if (job == NULL) {
		return false;
	}

	pthread_mutex_lock(&pool->mutex);
	// The user has moved on; only the newest preview is worth decoding
	drop_queued_previews(pool);
	job->next = pool->jobs_head;
	pool->jobs_head = job;
	if (pool->jobs_tail == NULL) {
		pool->jobs_tail = job;
	}
	pthread_cond_signal(&pool->wakeup);
	pthread_mutex_unlock(&pool->mutex);
	return true;
}

void matuwall_worker_drain(struct matuwall_worker_pool *pool,
	matuwall_result_fn cb, void *user_data) {
	uint64_t drained;
	while (read(pool->event_fd, &drained, sizeof(drained)) > 0) {
		// Clear the counter; the list below is the real work list
	}

	pthread_mutex_lock(&pool->mutex);
	struct job *list = pool->results;
	pool->results = NULL;
	pthread_mutex_unlock(&pool->mutex);

	// Reverse to restore submission order for a tidy fill
	struct job *ordered = NULL;
	while (list != NULL) {
		struct job *next = list->next;
		list->next = ordered;
		ordered = list;
		list = next;
	}

	while (ordered != NULL) {
		struct job *job = ordered;
		ordered = ordered->next;
		cb(user_data, &job->result);
		free(job);
	}
}

void matuwall_worker_pool_stop(struct matuwall_worker_pool *pool) {
	pthread_mutex_lock(&pool->mutex);
	pool->stopping = true;
	pthread_cond_broadcast(&pool->wakeup);
	pthread_mutex_unlock(&pool->mutex);

	for (size_t i = 0; i < pool->thread_count; i++) {
		pthread_join(pool->threads[i], NULL);
	}

	// Nothing is running now; free the queues without locking
	struct job *job = pool->jobs_head;
	while (job != NULL) {
		struct job *next = job->next;
		free(job);
		job = next;
	}
	job = pool->results;
	while (job != NULL) {
		struct job *next = job->next;
		free(job->result.pixels);
		free(job);
		job = next;
	}

	if (pool->event_fd >= 0) {
		close(pool->event_fd);
	}
	matuwall_cache_destroy(pool->cache);
	pthread_cond_destroy(&pool->wakeup);
	pthread_mutex_destroy(&pool->mutex);
	free(pool);
}
