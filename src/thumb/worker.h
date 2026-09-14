#ifndef MATUWALL_THUMB_WORKER_H
#define MATUWALL_THUMB_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum matuwall_thumb_state {
	MATUWALL_THUMB_PENDING,
	MATUWALL_THUMB_READY,
	MATUWALL_THUMB_FAILED,
};

// Previews are output sized and short lived, so they skip the on-disk cache
enum matuwall_job_kind {
	MATUWALL_JOB_THUMB,
	MATUWALL_JOB_PREVIEW,
};

struct matuwall_thumb {
	enum matuwall_thumb_state state;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

struct matuwall_thumb_result {
	enum matuwall_job_kind kind;
	size_t index;
	bool ok;
	bool cache_hit;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

typedef void (*matuwall_result_fn)(
	void *user_data, const struct matuwall_thumb_result *result);

struct matuwall_worker_pool;

struct matuwall_worker_pool *matuwall_worker_pool_start(
	uint32_t target_w, uint32_t target_h);

int matuwall_worker_pool_fd(const struct matuwall_worker_pool *pool);

bool matuwall_worker_submit(
	struct matuwall_worker_pool *pool, size_t index, const char *path);

// Move queued thumbnails in [first, end) and [0, wrap_end) ahead
void matuwall_worker_prioritize_thumbs(struct matuwall_worker_pool *pool,
	size_t first, size_t end, size_t wrap_end);

// Jumps the queue and drops any preview that has not started; latest wins
bool matuwall_worker_submit_preview(struct matuwall_worker_pool *pool,
	size_t index, const char *path, uint32_t target_w, uint32_t target_h);

void matuwall_worker_drain(struct matuwall_worker_pool *pool,
	matuwall_result_fn cb, void *user_data);

void matuwall_worker_pool_stop(struct matuwall_worker_pool *pool);

#endif
