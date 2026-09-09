#include "thumb/worker.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "thumb/cache.h"
#include "thumb/decode.h"
#include "thumb/scale.h"

#define MAX_WORKERS 4

struct job {
	enum sweetwall_job_kind kind;
	size_t index;
	char *path;
	uint32_t target_w;
	uint32_t target_h;
	struct job *next;
};

struct result {
	struct sweetwall_thumb_result data;
	struct result *next;
};

struct job_list {
	struct job *head;
	struct job *tail;
};

struct sweetwall_worker_pool {
	pthread_t threads[MAX_WORKERS];
	size_t thread_count;

	uint32_t target_w;
	uint32_t target_h;

	pthread_mutex_t mutex;
	pthread_cond_t wakeup;
	struct job *jobs_head;
	struct job *jobs_tail;
	struct result *results;
	bool stopping;

	int event_fd;
};

static size_t worker_count(void) {
	long online = sysconf(_SC_NPROCESSORS_ONLN);
	if (online < 1) {
		online = 1;
	}
	return online < MAX_WORKERS ? (size_t)online : MAX_WORKERS;
}

static bool produce(
	const struct job *job, struct sweetwall_image *out, bool *cache_hit) {
	*cache_hit = false;
	// Output-sized previews would bloat the cache for one keypress of value
	bool cached = job->kind == SWEETWALL_JOB_THUMB;

	char key[640];
	bool have_key = cached && sweetwall_cache_key(job->path, job->target_w,
					  job->target_h, key, sizeof(key));
	if (have_key && sweetwall_cache_read(key, out)) {
		*cache_hit = true;
		return true;
	}

	struct sweetwall_image decoded;
	if (!sweetwall_image_decode(
		    &decoded, job->path, job->target_w, job->target_h)) {
		return false;
	}
	bool scaled = sweetwall_scale_cover(
		&decoded, job->target_w, job->target_h, out);
	sweetwall_image_free(&decoded);
	if (!scaled) {
		return false;
	}

	if (have_key) {
		sweetwall_cache_write(key, out);
	}
	return true;
}

static struct job *take_job(struct sweetwall_worker_pool *pool) {
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

static void publish_result(
	struct sweetwall_worker_pool *pool, struct result *res) {
	pthread_mutex_lock(&pool->mutex);
	res->next = pool->results;
	pool->results = res;
	pthread_mutex_unlock(&pool->mutex);

	uint64_t one = 1;
	ssize_t written = write(pool->event_fd, &one, sizeof(one));
	(void)written;
}

static void *worker_main(void *arg) {
	struct sweetwall_worker_pool *pool = arg;

	pthread_mutex_lock(&pool->mutex);
	for (;;) {
		struct job *job = take_job(pool);
		if (job == NULL) {
			break;
		}
		pthread_mutex_unlock(&pool->mutex);

		struct result *res = calloc(1, sizeof(*res));
		if (res != NULL) {
			res->data.kind = job->kind;
			res->data.index = job->index;
			struct sweetwall_image img;
			if (produce(job, &img, &res->data.cache_hit)) {
				res->data.ok = true;
				res->data.pixels = img.pixels;
				res->data.width = img.width;
				res->data.height = img.height;
			}
			publish_result(pool, res);
		}

		free(job->path);
		free(job);
		pthread_mutex_lock(&pool->mutex);
	}
	pthread_mutex_unlock(&pool->mutex);
	return NULL;
}

struct sweetwall_worker_pool *sweetwall_worker_pool_start(
	uint32_t target_w, uint32_t target_h) {
	struct sweetwall_worker_pool *pool = calloc(1, sizeof(*pool));
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

	size_t want = worker_count();
	for (size_t i = 0; i < want; i++) {
		if (pthread_create(
			    &pool->threads[i], NULL, worker_main, pool) != 0) {
			break;
		}
		pool->thread_count++;
	}
	if (pool->thread_count == 0) {
		sweetwall_worker_pool_stop(pool);
		return NULL;
	}
	return pool;
}

int sweetwall_worker_pool_fd(const struct sweetwall_worker_pool *pool) {
	return pool->event_fd;
}

static struct job *make_job(enum sweetwall_job_kind kind, size_t index,
	const char *path, uint32_t target_w, uint32_t target_h) {
	struct job *job = calloc(1, sizeof(*job));
	if (job == NULL) {
		return NULL;
	}
	job->kind = kind;
	job->index = index;
	job->target_w = target_w;
	job->target_h = target_h;
	job->path = strdup(path);
	if (job->path == NULL) {
		free(job);
		return NULL;
	}
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
static void drop_queued_previews(struct sweetwall_worker_pool *pool) {
	struct job **cursor = &pool->jobs_head;
	struct job *prev = NULL;

	while (*cursor != NULL) {
		struct job *job = *cursor;
		if (job->kind != SWEETWALL_JOB_PREVIEW) {
			prev = job;
			cursor = &job->next;
			continue;
		}
		*cursor = job->next;
		if (pool->jobs_tail == job) {
			pool->jobs_tail = prev;
		}
		free(job->path);
		free(job);
	}
}

bool sweetwall_worker_submit(
	struct sweetwall_worker_pool *pool, size_t index, const char *path) {
	struct job *job = make_job(SWEETWALL_JOB_THUMB, index, path,
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

void sweetwall_worker_prioritize_thumbs(
	struct sweetwall_worker_pool *pool, size_t first, size_t end) {
	if (first >= end) {
		return;
	}

	struct job_list previews = {0};
	struct job_list visible = {0};
	struct job_list remaining = {0};

	pthread_mutex_lock(&pool->mutex);
	struct job *job = pool->jobs_head;
	while (job != NULL) {
		struct job *next = job->next;
		if (job->kind == SWEETWALL_JOB_PREVIEW) {
			job_list_append(&previews, job);
		} else if (job->index >= first && job->index < end) {
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

bool sweetwall_worker_submit_preview(struct sweetwall_worker_pool *pool,
	size_t index, const char *path, uint32_t target_w, uint32_t target_h) {
	struct job *job = make_job(
		SWEETWALL_JOB_PREVIEW, index, path, target_w, target_h);
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

void sweetwall_worker_drain(struct sweetwall_worker_pool *pool,
	sweetwall_result_fn cb, void *user_data) {
	uint64_t drained;
	while (read(pool->event_fd, &drained, sizeof(drained)) > 0) {
		// Clear the counter; the list below is the real work list
	}

	pthread_mutex_lock(&pool->mutex);
	struct result *list = pool->results;
	pool->results = NULL;
	pthread_mutex_unlock(&pool->mutex);

	// Reverse to restore submission order for a tidy fill
	struct result *ordered = NULL;
	while (list != NULL) {
		struct result *next = list->next;
		list->next = ordered;
		ordered = list;
		list = next;
	}

	while (ordered != NULL) {
		struct result *res = ordered;
		ordered = ordered->next;
		cb(user_data, &res->data);
		free(res);
	}
}

void sweetwall_worker_pool_stop(struct sweetwall_worker_pool *pool) {
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
		free(job->path);
		free(job);
		job = next;
	}
	struct result *res = pool->results;
	while (res != NULL) {
		struct result *next = res->next;
		free(res->data.pixels);
		free(res);
		res = next;
	}

	if (pool->event_fd >= 0) {
		close(pool->event_fd);
	}
	pthread_cond_destroy(&pool->wakeup);
	pthread_mutex_destroy(&pool->mutex);
	free(pool);
}
