#ifndef MATUWALL_THUMB_WORKER_H
#define MATUWALL_THUMB_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thumb/decode.h"

enum matuwall_thumb_state {
	MATUWALL_THUMB_UNLOADED,
	MATUWALL_THUMB_PENDING,
	MATUWALL_THUMB_READY,
	MATUWALL_THUMB_FAILED,
};

// previews are output sized and short lived, so they skip the on-disk cache
// colour jobs run only when nothing else is queued and return no pixels
enum matuwall_job_kind {
	MATUWALL_JOB_THUMB,
	MATUWALL_JOB_PREVIEW,
	MATUWALL_JOB_COLOR,
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
	bool cancelled;
	// size the job was asked for, a retarget can make it stale
	uint32_t target_w;
	uint32_t target_h;
	// thumbnail colour key when the pool computes them, else unknown
	uint32_t hue;
	// owned by the drain callback; a preview's is always memfd backed
	struct matuwall_image image;
};

typedef void (*matuwall_result_fn)(
	void *user_data, const struct matuwall_thumb_result *result);

struct matuwall_worker_pool;

// hues adds a colour key to every thumbnail result
struct matuwall_worker_pool *matuwall_worker_pool_start(
	uint32_t target_w, uint32_t target_h, bool hues);

int matuwall_worker_pool_fd(const struct matuwall_worker_pool *pool);

// Paths passed to either submit function are borrowed until pool stop
bool matuwall_worker_submit(
	struct matuwall_worker_pool *pool, size_t index, const char *path);

// background colour key for one entry; reorders never touch these
bool matuwall_worker_submit_color(
	struct matuwall_worker_pool *pool, size_t index, const char *path);

// callbacks for a queue reorder, keep runs under the pool lock
struct matuwall_thumb_filter {
	bool (*keep)(void *user_data, size_t index);
	void (*dropped)(void *user_data, size_t index);
	void *user_data;
};

// Move queued thumbnails in [first, end) and [0, wrap_end) ahead and
// withdraw queued ones keep rejects, dropped runs after the lock is released
void matuwall_worker_prioritize_thumbs(struct matuwall_worker_pool *pool,
	size_t first, size_t end, size_t wrap_end,
	const struct matuwall_thumb_filter *filter);

// queued thumbnail and colour jobs take the new size, running ones keep theirs
void matuwall_worker_retarget(struct matuwall_worker_pool *pool,
	uint32_t target_w, uint32_t target_h);

// jumps the queue, drops queued previews and cancels a running one
bool matuwall_worker_submit_preview(struct matuwall_worker_pool *pool,
	size_t index, const char *path, uint32_t target_w, uint32_t target_h);

// cancelled preview still publishes a result, with ok false
void matuwall_worker_cancel_preview(struct matuwall_worker_pool *pool);

void matuwall_worker_drain(struct matuwall_worker_pool *pool,
	matuwall_result_fn cb, void *user_data);

// no new job starts, running WebP and progressive JPEG setup still finish
void matuwall_worker_pool_request_stop(struct matuwall_worker_pool *pool);

// requests a stop, joins every worker and frees the pool
void matuwall_worker_pool_stop(struct matuwall_worker_pool *pool);

#endif
