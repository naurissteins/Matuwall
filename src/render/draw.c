#include "render/draw.h"

#include <math.h>
#include <stdbool.h>

#define COVERAGE_MAX 255

static inline uint32_t div255(uint32_t value) {
	value += 0x80;
	return (value + (value >> 8)) >> 8;
}

static inline bool is_opaque(uint32_t color) {
	return (color >> 24) == 0xff;
}

static inline uint32_t blend(uint32_t dst, uint32_t src, uint32_t coverage) {
	if (coverage == 0) {
		return dst;
	}
	if (coverage == COVERAGE_MAX && is_opaque(src)) {
		return src;
	}

	uint32_t sa = div255(((src >> 24) & 0xff) * coverage);
	uint32_t sr = div255(((src >> 16) & 0xff) * coverage);
	uint32_t sg = div255(((src >> 8) & 0xff) * coverage);
	uint32_t sb = div255((src & 0xff) * coverage);

	uint32_t inv = 255 - sa;
	uint32_t a = sa + div255(((dst >> 24) & 0xff) * inv);
	uint32_t r = sr + div255(((dst >> 16) & 0xff) * inv);
	uint32_t g = sg + div255(((dst >> 8) & 0xff) * inv);
	uint32_t b = sb + div255((dst & 0xff) * inv);

	return a << 24 | r << 16 | g << 8 | b;
}

struct sweetwall_clip sweetwall_clip_buffer(
	const struct sweetwall_buffer *buffer) {
	return (struct sweetwall_clip){
		.x1 = (int32_t)buffer->width,
		.y1 = (int32_t)buffer->height,
	};
}

void sweetwall_draw_clear(struct sweetwall_buffer *buffer, uint32_t color) {
	size_t count = (size_t)buffer->width * buffer->height;
	for (size_t i = 0; i < count; i++) {
		buffer->data[i] = color;
	}
}

static void blend_span(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t y, int32_t x0, int32_t x1,
	uint32_t color) {
	if (y < clip->y0 || y >= clip->y1) {
		return;
	}
	if (x0 < clip->x0) {
		x0 = clip->x0;
	}
	if (x1 > clip->x1) {
		x1 = clip->x1;
	}

	uint32_t *row = buffer->data + (size_t)y * buffer->width;
	if (is_opaque(color)) {
		for (int32_t x = x0; x < x1; x++) {
			row[x] = color;
		}
		return;
	}
	for (int32_t x = x0; x < x1; x++) {
		row[x] = blend(row[x], color, COVERAGE_MAX);
	}
}

static uint32_t corner_coverage(
	int32_t px, int32_t py, double cx, double cy, double radius) {
	double dx = ((double)px + 0.5) - cx;
	double dy = ((double)py + 0.5) - cy;
	double distance = sqrt(dx * dx + dy * dy);

	// One-pixel linear ramp across the edge
	double coverage = radius + 0.5 - distance;
	if (coverage <= 0.0) {
		return 0;
	}
	if (coverage >= 1.0) {
		return COVERAGE_MAX;
	}
	return (uint32_t)(coverage * COVERAGE_MAX + 0.5);
}

static void blend_corner_row(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t y, int32_t x0, int32_t x1,
	double cx, double cy, double radius, uint32_t color) {
	if (y < clip->y0 || y >= clip->y1) {
		return;
	}
	int32_t start = x0 < clip->x0 ? clip->x0 : x0;
	int32_t end = x1 > clip->x1 ? clip->x1 : x1;

	uint32_t *row = buffer->data + (size_t)y * buffer->width;
	for (int32_t x = start; x < end; x++) {
		uint32_t coverage = corner_coverage(x, y, cx, cy, radius);
		row[x] = blend(row[x], color, coverage);
	}
}

void sweetwall_draw_rounded_rect(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color) {
	if (width <= 0 || height <= 0) {
		return;
	}

	int32_t limit = (width < height ? width : height) / 2;
	if (radius > limit) {
		radius = limit;
	}
	if (radius < 0) {
		radius = 0;
	}

	int32_t right = x + width;
	int32_t bottom = y + height;

	// Straight middle band: no curvature, so no per-pixel distance work
	for (int32_t row = y + radius; row < bottom - radius; row++) {
		blend_span(buffer, clip, row, x, right, color);
	}

	if (radius == 0) {
		return;
	}

	double r = (double)radius;
	double left_c = (double)x + r;
	double right_c = (double)right - r;
	double top_c = (double)y + r;
	double bottom_c = (double)bottom - r;

	for (int32_t offset = 0; offset < radius; offset++) {
		int32_t top = y + offset;
		int32_t bot = bottom - 1 - offset;

		blend_span(
			buffer, clip, top, x + radius, right - radius, color);
		blend_span(
			buffer, clip, bot, x + radius, right - radius, color);

		blend_corner_row(buffer, clip, top, x, x + radius, left_c,
			top_c, r, color);
		blend_corner_row(buffer, clip, top, right - radius, right,
			right_c, top_c, r, color);
		blend_corner_row(buffer, clip, bot, x, x + radius, left_c,
			bottom_c, r, color);
		blend_corner_row(buffer, clip, bot, right - radius, right,
			right_c, bottom_c, r, color);
	}
}
