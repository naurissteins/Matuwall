#ifndef SWEETWALL_THUMB_WORKER_H
#define SWEETWALL_THUMB_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum sweetwall_thumb_state {
	SWEETWALL_THUMB_PENDING,
	SWEETWALL_THUMB_READY,
	SWEETWALL_THUMB_FAILED,
};

// Previews are output sized and short lived, so they skip the on-disk cache
enum sweetwall_job_kind {
	SWEETWALL_JOB_THUMB,
	SWEETWALL_JOB_PREVIEW,
};

struct sweetwall_thumb {
	enum sweetwall_thumb_state state;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

struct sweetwall_thumb_result {
	enum sweetwall_job_kind kind;
	size_t index;
	bool ok;
	bool cache_hit;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

typedef void (*sweetwall_result_fn)(
	void *user_data, const struct sweetwall_thumb_result *result);

struct sweetwall_worker_pool;

struct sweetwall_worker_pool *sweetwall_worker_pool_start(
	uint32_t target_w, uint32_t target_h);

int sweetwall_worker_pool_fd(const struct sweetwall_worker_pool *pool);

bool sweetwall_worker_submit(
	struct sweetwall_worker_pool *pool, size_t index, const char *path);

// Move queued thumbnails in [first, end) ahead of other thumbnail jobs
void sweetwall_worker_prioritize_thumbs(
	struct sweetwall_worker_pool *pool, size_t first, size_t end);

// Jumps the queue and drops any preview that has not started; latest wins
bool sweetwall_worker_submit_preview(struct sweetwall_worker_pool *pool,
	size_t index, const char *path, uint32_t target_w, uint32_t target_h);

void sweetwall_worker_drain(struct sweetwall_worker_pool *pool,
	sweetwall_result_fn cb, void *user_data);

void sweetwall_worker_pool_stop(struct sweetwall_worker_pool *pool);

#endif
