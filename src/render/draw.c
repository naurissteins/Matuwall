#include "render/draw.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

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

// A radius past half the shorter side is a pill, not a bigger curve. Every
// rounded primitive clamps the same way
static int32_t clamp_radius(int32_t radius, int32_t width, int32_t height) {
	int32_t limit = (width < height ? width : height) / 2;
	if (radius > limit) {
		radius = limit;
	}
	return radius < 0 ? 0 : radius;
}

struct sweetwall_clip sweetwall_clip_buffer(
	const struct sweetwall_buffer *buffer) {
	return (struct sweetwall_clip){
		.x1 = (int32_t)buffer->width,
		.y1 = (int32_t)buffer->height,
	};
}

void sweetwall_draw_clear(struct sweetwall_buffer *buffer, uint32_t color) {
	// A fresh mapping is already zero; writing it just faults in every
	// page, which on a full-output surface dominates the first frame
	if (color == 0 && buffer->fresh) {
		return;
	}
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

static uint32_t rect_coverage(int32_t px, int32_t py, double x, double y,
	double width, double height, double radius) {
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
	double distance = sqrt(outside_x * outside_x + outside_y * outside_y) +
			  inside - radius;
	double coverage = 0.5 - distance;
	if (coverage <= 0.0) {
		return 0;
	}
	if (coverage >= 1.0) {
		return COVERAGE_MAX;
	}
	return (uint32_t)(coverage * COVERAGE_MAX + 0.5);
}

void sweetwall_draw_rounded_ring(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t thickness, uint32_t color) {
	if (width <= 0 || height <= 0 || thickness <= 0) {
		return;
	}
	if (thickness * 2 > width || thickness * 2 > height) {
		return;
	}
	radius = clamp_radius(radius, width, height);

	double inner_x = x + thickness;
	double inner_y = y + thickness;
	double inner_w = width - thickness * 2;
	double inner_h = height - thickness * 2;
	double inner_r = radius - thickness;
	if (inner_r < 0.0) {
		inner_r = 0.0;
	}

	int32_t inner_left = (int32_t)inner_x;
	int32_t inner_right = (int32_t)(inner_x + inner_w);
	int32_t inner_top = (int32_t)inner_y;
	int32_t inner_bottom = (int32_t)(inner_y + inner_h);
	int32_t core_left = (int32_t)(inner_x + inner_r);
	int32_t core_right = (int32_t)(inner_x + inner_w - inner_r);
	int32_t core_top = (int32_t)(inner_y + inner_r);
	int32_t core_bottom = (int32_t)(inner_y + inner_h - inner_r);

	int32_t top = y < clip->y0 ? clip->y0 : y;
	int32_t bottom = y + height > clip->y1 ? clip->y1 : y + height;
	int32_t left = x < clip->x0 ? clip->x0 : x;
	int32_t right = x + width > clip->x1 ? clip->x1 : x + width;

	for (int32_t py = top; py < bottom; py++) {
		int32_t skip_x0 = left;
		int32_t skip_x1 = left;
		if (py >= inner_top && py < inner_bottom) {
			bool core_row = py >= core_top && py < core_bottom;
			skip_x0 = core_row ? inner_left : core_left;
			skip_x1 = core_row ? inner_right : core_right;
		}
		uint32_t *row = buffer->data + (size_t)py * buffer->width;

		for (int32_t px = left; px < right; px++) {
			if (px >= skip_x0 && px < skip_x1) {
				// Jump the interior in one step
				px = skip_x1 - 1;
				continue;
			}

			uint32_t outer = rect_coverage(
				px, py, x, y, width, height, radius);
			if (outer == 0) {
				continue;
			}
			uint32_t inner = rect_coverage(px, py, inner_x, inner_y,
				inner_w, inner_h, inner_r);
			if (inner >= outer) {
				continue;
			}
			row[px] = blend(row[px], color, outer - inner);
		}
	}
}

void sweetwall_draw_rounded_rect(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color) {
	if (width <= 0 || height <= 0) {
		return;
	}

	radius = clamp_radius(radius, width, height);

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

// Fixed-point source stepping keeps the full-screen blit off the divider
#define COVER_SHIFT 16

void sweetwall_draw_image_cover(struct sweetwall_buffer *buffer,
	const uint32_t *src, uint32_t src_w, uint32_t src_h) {
	if (src == NULL || src_w == 0 || src_h == 0 || buffer->width == 0 ||
		buffer->height == 0) {
		return;
	}

	// The scaler already covers the buffer exactly in the common case
	if (src_w == buffer->width && src_h == buffer->height) {
		memcpy(buffer->data, src,
			(size_t)src_w * src_h * sizeof(uint32_t));
		return;
	}

	uint32_t crop_w = src_w;
	uint32_t crop_h = src_h;
	if ((uint64_t)src_w * buffer->height >
		(uint64_t)buffer->width * src_h) {
		crop_w = (uint32_t)((uint64_t)buffer->width * src_h /
				    buffer->height);
	} else {
		crop_h = (uint32_t)((uint64_t)buffer->height * src_w /
				    buffer->width);
	}
	if (crop_w == 0) {
		crop_w = 1;
	}
	if (crop_h == 0) {
		crop_h = 1;
	}

	uint32_t off_x = (src_w - crop_w) / 2;
	uint32_t off_y = (src_h - crop_h) / 2;
	uint64_t step_x = ((uint64_t)crop_w << COVER_SHIFT) / buffer->width;

	for (uint32_t y = 0; y < buffer->height; y++) {
		uint32_t sy = off_y +
			      (uint32_t)((uint64_t)y * crop_h / buffer->height);
		if (sy >= src_h) {
			sy = src_h - 1;
		}
		const uint32_t *src_row = src + (size_t)sy * src_w;
		uint32_t *dst_row = buffer->data + (size_t)y * buffer->width;

		uint64_t pos = 0;
		for (uint32_t x = 0; x < buffer->width; x++) {
			uint32_t sx = off_x + (uint32_t)(pos >> COVER_SHIFT);
			pos += step_x;
			if (sx >= src_w) {
				sx = src_w - 1;
			}
			dst_row[x] = src_row[sx];
		}
	}
}

void sweetwall_draw_image_rounded(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h) {
	if (width <= 0 || height <= 0 || inset < 0 || inset > (width - 1) / 2 ||
		inset > (height - 1) / 2 || src == NULL || src_w == 0 ||
		src_h == 0) {
		return;
	}

	radius = clamp_radius(radius, width, height);
	int32_t inner_radius = radius > inset ? radius - inset : 0;
	int32_t inner_x = x + inset;
	int32_t inner_y = y + inset;
	int32_t inner_width = width - inset * 2;
	int32_t inner_height = height - inset * 2;

	int32_t top = inner_y < clip->y0 ? clip->y0 : inner_y;
	int32_t bottom = inner_y + inner_height > clip->y1
				 ? clip->y1
				 : inner_y + inner_height;
	int32_t left = inner_x < clip->x0 ? clip->x0 : inner_x;
	int32_t right = inner_x + inner_width > clip->x1
				? clip->x1
				: inner_x + inner_width;

	for (int32_t py = top; py < bottom; py++) {
		uint32_t sy = (uint32_t)((int64_t)(py - y) * src_h / height);
		if (sy >= src_h) {
			sy = src_h - 1;
		}
		const uint32_t *src_row = src + (size_t)sy * src_w;
		uint32_t *dst_row = buffer->data + (size_t)py * buffer->width;

		for (int32_t px = left; px < right; px++) {
			uint32_t cov = rect_coverage(px, py, inner_x, inner_y,
				inner_width, inner_height, inner_radius);
			if (cov == 0) {
				continue;
			}
			uint32_t sx =
				(uint32_t)((int64_t)(px - x) * src_w / width);
			if (sx >= src_w) {
				sx = src_w - 1;
			}
			dst_row[px] = blend(dst_row[px], src_row[sx], cov);
		}
	}
}
