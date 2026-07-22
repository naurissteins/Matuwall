#ifndef SWEETWALL_RENDER_DRAW_H
#define SWEETWALL_RENDER_DRAW_H

#include <stdint.h>

#include "wayland/shm.h"

struct sweetwall_clip {
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
};

struct sweetwall_clip sweetwall_clip_buffer(
	const struct sweetwall_buffer *buffer);

void sweetwall_draw_clear(struct sweetwall_buffer *buffer, uint32_t color);

void sweetwall_draw_rounded_rect(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color);

#endif
