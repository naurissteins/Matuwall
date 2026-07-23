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

struct sweetwall_thumb {
	enum sweetwall_thumb_state state;
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
};

struct sweetwall_thumb_result {
	size_t index;
	bool ok;
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

void sweetwall_worker_drain(struct sweetwall_worker_pool *pool,
	sweetwall_result_fn cb, void *user_data);

void sweetwall_worker_pool_stop(struct sweetwall_worker_pool *pool);

#endif
