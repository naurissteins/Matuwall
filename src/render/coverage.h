#ifndef MATUWALL_RENDER_COVERAGE_H
#define MATUWALL_RENDER_COVERAGE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "render/draw.h"

// Compositing and antialiasing math shared by shape and image primitives

#define COVERAGE_MAX 255

static inline uint32_t div255(uint32_t value) {
	value += 0x80;
	return (value + (value >> 8)) >> 8;
}

static inline bool is_opaque(uint32_t color) {
	return (color >> 24) == 0xff;
}

// A premultiplied source already scaled by its coverage
struct ink {
	uint32_t src;
	uint32_t inv;
};

static inline struct ink ink_at(uint32_t src, uint32_t coverage) {
	uint32_t sa = div255(((src >> 24) & 0xff) * coverage);
	uint32_t sr = div255(((src >> 16) & 0xff) * coverage);
	uint32_t sg = div255(((src >> 8) & 0xff) * coverage);
	uint32_t sb = div255((src & 0xff) * coverage);
	return (struct ink){
		.src = sa << 24 | sr << 16 | sg << 8 | sb,
		.inv = 255 - sa,
	};
}

// div255 on two 16-bit lanes at once; dst * inv + 0x80 never carries a lane
static inline uint32_t blend_ink(uint32_t dst, struct ink ink) {
	uint32_t rb = (dst & 0x00ff00ff) * ink.inv + 0x00800080;
	uint32_t ag = ((dst >> 8) & 0x00ff00ff) * ink.inv + 0x00800080;
	rb = ((rb + ((rb >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff;
	ag = (ag + ((ag >> 8) & 0x00ff00ff)) & 0xff00ff00;
	// premultiplied ink keeps every channel sum within 255
	return ink.src + (ag | rb);
}

static inline uint32_t blend(uint32_t dst, uint32_t src, uint32_t coverage) {
	if (coverage == 0) {
		return dst;
	}
	if (coverage == COVERAGE_MAX && is_opaque(src)) {
		return src;
	}
	return blend_ink(dst, ink_at(src, coverage));
}

static inline uint32_t blend_opaque(
	uint32_t dst, uint32_t src, uint32_t opacity) {
	uint32_t keep = COVERAGE_MAX - opacity;
	uint32_t rb = (src & 0x00ff00ff) * opacity + (dst & 0x00ff00ff) * keep +
		      0x00800080;
	uint32_t ag = ((src >> 8) & 0x00ff00ff) * opacity +
		      ((dst >> 8) & 0x00ff00ff) * keep + 0x00800080;
	rb = ((rb + ((rb >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff;
	ag = (ag + ((ag >> 8) & 0x00ff00ff)) & 0xff00ff00;
	return ag | rb;
}

// Whole-primitive opacity; COVERAGE_MAX is the identity
static inline uint32_t fade_coverage(uint32_t coverage, uint32_t opacity) {
	return opacity == COVERAGE_MAX ? coverage : div255(coverage * opacity);
}

// A radius past half the shorter side is a pill, not a bigger curve. Every
// rounded primitive clamps the same way
static inline int32_t clamp_radius(
	int32_t radius, int32_t width, int32_t height) {
	int32_t limit = (width < height ? width : height) / 2;
	if (radius > limit) {
		radius = limit;
	}
	return radius < 0 ? 0 : radius;
}

static inline bool clip_to_buffer(
	struct matuwall_clip *clip, const struct matuwall_buffer *buffer) {
	if (clip->x0 < 0) {
		clip->x0 = 0;
	}
	if (clip->y0 < 0) {
		clip->y0 = 0;
	}
	if (clip->x1 > (int32_t)buffer->width) {
		clip->x1 = (int32_t)buffer->width;
	}
	if (clip->y1 > (int32_t)buffer->height) {
		clip->y1 = (int32_t)buffer->height;
	}
	return clip->x0 < clip->x1 && clip->y0 < clip->y1;
}

static inline double rounded_rect_distance(int32_t px, int32_t py, double x,
	double y, double width, double height, double radius) {
	double half_w = width / 2.0;
	double half_h = height / 2.0;
	double dx = fabs(((double)px + 0.5) - (x + half_w)) - (half_w - radius);
	double dy = fabs(((double)py + 0.5) - (y + half_h)) - (half_h - radius);
	double outside_x = dx > 0.0 ? dx : 0.0;
	double outside_y = dy > 0.0 ? dy : 0.0;
	double inside = dx > dy ? dx : dy;
	if (inside > 0.0) {
		inside = 0.0;
	}
	// Only corners need a hypotenuse; sqrt(a * a) is exactly a
	if (outside_y == 0.0) {
		return outside_x + inside - radius;
	}
	if (outside_x == 0.0) {
		return outside_y + inside - radius;
	}
	return sqrt(outside_x * outside_x + outside_y * outside_y) + inside -
	       radius;
}

static inline uint32_t rect_coverage(int32_t px, int32_t py, double x, double y,
	double width, double height, double radius) {
	double distance =
		rounded_rect_distance(px, py, x, y, width, height, radius);
	double coverage = 0.5 - distance;
	if (coverage <= 0.0) {
		return 0;
	}
	if (coverage >= 1.0) {
		return COVERAGE_MAX;
	}
	return (uint32_t)(coverage * COVERAGE_MAX + 0.5);
}

#endif
