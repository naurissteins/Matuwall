#ifndef MATUWALL_WAYLAND_SHM_H
#define MATUWALL_WAYLAND_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_buffer;
struct wl_shm;

// buffer pixels, x1 and y1 exclusive
struct matuwall_clip {
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
};

struct matuwall_buffer {
	struct wl_buffer *wl_buffer;
	uint32_t *data;
	size_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	bool released;
	// wraps a caller's memfd: never mapped here, never drawn into again
	bool adopted;
	// Untouched since mmap, so every pixel is still zero
	bool fresh;
	// Last rendered scene state advances independently per buffer
	bool frame_valid;
	struct matuwall_clip panel_damage;
	struct matuwall_buffer *next;
};

enum matuwall_buffer_acquire {
	MATUWALL_BUFFER_READY,
	MATUWALL_BUFFER_BUSY,
	MATUWALL_BUFFER_FAILED,
};

struct matuwall_buffer_pool {
	struct matuwall_buffer *buffers;
	// Borrowed from buffers until submission or reclamation
	struct matuwall_buffer *drawing;
};

enum matuwall_buffer_acquire matuwall_buffer_pool_acquire(
	struct matuwall_buffer_pool *pool, struct wl_shm *shm, uint32_t width,
	uint32_t height, struct matuwall_buffer **buffer);

// wraps a caller-filled memfd without mapping it; the caller keeps fd
enum matuwall_buffer_acquire matuwall_buffer_pool_adopt(
	struct matuwall_buffer_pool *pool, struct wl_shm *shm, int fd,
	uint32_t width, uint32_t height, struct matuwall_buffer **buffer);

// requires a live drawing buffer from a successful acquire or adopt
void matuwall_buffer_pool_submitted(struct matuwall_buffer_pool *pool);

void matuwall_buffer_pool_collect_idle(
	struct matuwall_buffer_pool *pool, uint32_t width, uint32_t height);

void matuwall_buffer_pool_destroy(struct matuwall_buffer_pool *pool);

// one transparent 1x1 pixel, caller destroys the returned wl_buffer
struct wl_buffer *matuwall_shm_clear_pixel(struct wl_shm *shm);

#endif
