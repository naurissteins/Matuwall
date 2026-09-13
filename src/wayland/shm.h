#ifndef SWEETWALL_WAYLAND_SHM_H
#define SWEETWALL_WAYLAND_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_buffer;
struct wl_shm;

struct sweetwall_buffer {
	struct wl_buffer *wl_buffer;
	uint32_t *data;
	size_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	bool released;
	// Untouched since mmap, so every pixel is still zero
	bool fresh;
	struct sweetwall_buffer *next;
};

enum sweetwall_buffer_acquire {
	SWEETWALL_BUFFER_READY,
	SWEETWALL_BUFFER_BUSY,
	SWEETWALL_BUFFER_FAILED,
};

struct sweetwall_buffer_pool {
	struct sweetwall_buffer *buffers;
	struct sweetwall_buffer *drawing;
};

bool sweetwall_buffer_create(struct sweetwall_buffer *buffer,
	struct wl_shm *shm, uint32_t width, uint32_t height);

// Fill every pixel with one premultiplied ARGB8888 color
void sweetwall_buffer_fill(struct sweetwall_buffer *buffer, uint32_t color);

// Unmap the pixels and destroy the wl_buffer. Idempotent
void sweetwall_buffer_destroy(struct sweetwall_buffer *buffer);

enum sweetwall_buffer_acquire sweetwall_buffer_pool_acquire(
	struct sweetwall_buffer_pool *pool, struct wl_shm *shm, uint32_t width,
	uint32_t height, struct sweetwall_buffer **buffer);

void sweetwall_buffer_pool_submitted(struct sweetwall_buffer_pool *pool);

void sweetwall_buffer_pool_collect_idle(
	struct sweetwall_buffer_pool *pool, uint32_t width, uint32_t height);

void sweetwall_buffer_pool_destroy(struct sweetwall_buffer_pool *pool);

#endif
