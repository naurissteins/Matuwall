#include "wayland/shm.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#define BYTES_PER_PIXEL 4
#define MAX_GEOMETRY_BUFFERS 2

static void handle_release(void *data, struct wl_buffer *wl_buffer) {
	struct matuwall_buffer *buffer = data;
	(void)wl_buffer;

	buffer->released = true;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = handle_release,
};

// Reject dimensions that would overflow the stride or total size math before
// anything is allocated or indexed with them
static bool buffer_size(uint32_t width, uint32_t height, uint32_t *stride_out,
	size_t *size_out) {
	if (width == 0 || height == 0) {
		return false;
	}
	if (width > (uint32_t)INT32_MAX / BYTES_PER_PIXEL) {
		return false;
	}
	uint32_t stride = width * BYTES_PER_PIXEL;
	if (height > (uint32_t)INT32_MAX / stride) {
		return false;
	}
	*stride_out = stride;
	*size_out = (size_t)stride * height;
	return true;
}

static bool matuwall_buffer_create(struct matuwall_buffer *buffer,
	struct wl_shm *shm, uint32_t width, uint32_t height) {
	*buffer = (struct matuwall_buffer){0};

	uint32_t stride;
	size_t size;
	if (!buffer_size(width, height, &stride, &size)) {
		return false;
	}

	int fd = memfd_create("matuwall-shm", MFD_CLOEXEC);
	if (fd < 0) {
		return false;
	}
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return false;
	}

	void *data =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return false;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
	if (pool == NULL) {
		munmap(data, size);
		close(fd);
		return false;
	}

	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)width,
		(int32_t)height, (int32_t)stride, WL_SHM_FORMAT_ARGB8888);

	wl_shm_pool_destroy(pool);
	close(fd);

	if (buffer->wl_buffer == NULL) {
		munmap(data, size);
		return false;
	}

	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	buffer->data = data;
	buffer->size = size;
	buffer->width = width;
	buffer->height = height;
	buffer->stride = stride;
	buffer->released = true;
	buffer->fresh = true;
	return true;
}

static void matuwall_buffer_destroy(struct matuwall_buffer *buffer) {
	if (buffer->wl_buffer != NULL) {
		wl_buffer_destroy(buffer->wl_buffer);
		buffer->wl_buffer = NULL;
	}
	if (buffer->data != NULL) {
		munmap(buffer->data, buffer->size);
		buffer->data = NULL;
	}
	buffer->size = 0;
	buffer->released = true;
}

static void free_buffer(
	struct matuwall_buffer_pool *pool, struct matuwall_buffer *buffer) {
	if (pool->drawing == buffer) {
		pool->drawing = NULL;
	}
	matuwall_buffer_destroy(buffer);
	free(buffer);
}

static void collect_stale(
	struct matuwall_buffer_pool *pool, uint32_t width, uint32_t height) {
	struct matuwall_buffer **cursor = &pool->buffers;
	while (*cursor != NULL) {
		struct matuwall_buffer *buffer = *cursor;
		bool matches =
			buffer->width == width && buffer->height == height;
		if (!buffer->released || matches) {
			cursor = &buffer->next;
			continue;
		}
		*cursor = buffer->next;
		free_buffer(pool, buffer);
	}
}

enum matuwall_buffer_acquire matuwall_buffer_pool_acquire(
	struct matuwall_buffer_pool *pool, struct wl_shm *shm, uint32_t width,
	uint32_t height, struct matuwall_buffer **out) {
	*out = NULL;
	collect_stale(pool, width, height);

	size_t matching = 0;
	for (struct matuwall_buffer *buffer = pool->buffers; buffer != NULL;
		buffer = buffer->next) {
		if (buffer->width != width || buffer->height != height) {
			continue;
		}
		matching++;
		if (buffer->released) {
			pool->drawing = buffer;
			*out = buffer;
			return MATUWALL_BUFFER_READY;
		}
	}
	if (matching >= MAX_GEOMETRY_BUFFERS) {
		return MATUWALL_BUFFER_BUSY;
	}

	struct matuwall_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL ||
		!matuwall_buffer_create(buffer, shm, width, height)) {
		free(buffer);
		return MATUWALL_BUFFER_FAILED;
	}
	buffer->next = pool->buffers;
	pool->buffers = buffer;
	pool->drawing = buffer;
	*out = buffer;
	return MATUWALL_BUFFER_READY;
}

void matuwall_buffer_pool_submitted(struct matuwall_buffer_pool *pool) {
	assert(pool->drawing != NULL);
	pool->drawing->released = false;
	pool->drawing->fresh = false;
	pool->drawing = NULL;
}

void matuwall_buffer_pool_collect_idle(
	struct matuwall_buffer_pool *pool, uint32_t width, uint32_t height) {
	bool busy_current = false;
	for (struct matuwall_buffer *buffer = pool->buffers; buffer != NULL;
		buffer = buffer->next) {
		if (buffer->width == width && buffer->height == height &&
			!buffer->released) {
			busy_current = true;
		}
	}

	bool kept_released = false;
	struct matuwall_buffer **cursor = &pool->buffers;
	while (*cursor != NULL) {
		struct matuwall_buffer *buffer = *cursor;
		bool current =
			buffer->width == width && buffer->height == height;
		bool keep = !buffer->released ||
			    (current && !busy_current && !kept_released);
		if (keep) {
			kept_released |= current && buffer->released;
			cursor = &buffer->next;
			continue;
		}
		*cursor = buffer->next;
		free_buffer(pool, buffer);
	}
}

void matuwall_buffer_pool_destroy(struct matuwall_buffer_pool *pool) {
	while (pool->buffers != NULL) {
		struct matuwall_buffer *buffer = pool->buffers;
		pool->buffers = buffer->next;
		free_buffer(pool, buffer);
	}
	pool->drawing = NULL;
}
