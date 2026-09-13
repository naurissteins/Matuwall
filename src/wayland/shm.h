#ifndef MATUWALL_WAYLAND_SHM_H
#define MATUWALL_WAYLAND_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_buffer;
struct wl_shm;

struct matuwall_buffer {
	struct wl_buffer *wl_buffer;
	uint32_t *data;
	size_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	bool released;
	// Untouched since mmap, so every pixel is still zero
	bool fresh;
	struct matuwall_buffer *next;
};

enum matuwall_buffer_acquire {
	MATUWALL_BUFFER_READY,
	MATUWALL_BUFFER_BUSY,
	MATUWALL_BUFFER_FAILED,
};

struct matuwall_buffer_pool {
	struct matuwall_buffer *buffers;
	struct matuwall_buffer *drawing;
};

bool matuwall_buffer_create(struct matuwall_buffer *buffer, struct wl_shm *shm,
	uint32_t width, uint32_t height);

// Fill every pixel with one premultiplied ARGB8888 color
void matuwall_buffer_fill(struct matuwall_buffer *buffer, uint32_t color);

// Unmap the pixels and destroy the wl_buffer. Idempotent
void matuwall_buffer_destroy(struct matuwall_buffer *buffer);

enum matuwall_buffer_acquire matuwall_buffer_pool_acquire(
	struct matuwall_buffer_pool *pool, struct wl_shm *shm, uint32_t width,
	uint32_t height, struct matuwall_buffer **buffer);

void matuwall_buffer_pool_submitted(struct matuwall_buffer_pool *pool);

void matuwall_buffer_pool_collect_idle(
	struct matuwall_buffer_pool *pool, uint32_t width, uint32_t height);

void matuwall_buffer_pool_destroy(struct matuwall_buffer_pool *pool);

#endif
