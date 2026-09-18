#ifndef MATUWALL_RENDER_DRAW_H
#define MATUWALL_RENDER_DRAW_H

#include <stdint.h>

#include "wayland/shm.h"

struct matuwall_clip {
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
};

void matuwall_draw_clear_clipped(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, uint32_t color);

void matuwall_draw_rounded_rect(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color);

void matuwall_draw_rounded_ring(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t thickness, uint32_t color);

void matuwall_draw_rounded_shadow(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t shadow_width, uint32_t color);

// Fill from opaque ARGB8888 src, center-cropped to the buffer aspect
void matuwall_draw_image_cover_clipped(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, const uint32_t *src, uint32_t src_w,
	uint32_t src_h);

// Source is opaque ARGB8888; inset clips without changing source mapping
void matuwall_draw_image_rounded(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h);

void matuwall_draw_image_rounded_bilinear(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h);

#endif
