#include "render/draw.h"

#include <math.h>
#include <stdbool.h>

#include "render/coverage.h"

// --- rounded fills ---

static void clear_clipped(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, uint32_t color) {
	// A fresh mapping is already zero; writing it just faults in every
	// page, which on a full-output surface dominates the first frame
	if (color == 0 && buffer->fresh) {
		return;
	}
	struct matuwall_clip clipped = *clip;
	if (!clip_to_buffer(&clipped, buffer)) {
		return;
	}
	for (int32_t y = clipped.y0; y < clipped.y1; y++) {
		uint32_t *row = buffer->data + (size_t)y * buffer->width;
		for (int32_t x = clipped.x0; x < clipped.x1; x++) {
			row[x] = color;
		}
	}
}

static void blend_span(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t y, int32_t x0, int32_t x1,
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

// the top-left corner of a rect at 0, 0 sees the same exact offsets as any
static bool corner_table_fill(struct corner_table *table, int32_t radius) {
	if (radius <= 0 || radius > CORNER_TABLE_MAX) {
		return false;
	}
	double r = (double)radius;
	table->radius = radius;
	for (int32_t j = 0; j < radius; j++) {
		for (int32_t i = 0; i < radius; i++) {
			table->coverage[(size_t)j * (size_t)radius +
					(size_t)i] =
				(uint8_t)corner_coverage(i, j, r, r, r);
		}
	}
	return true;
}

// a rounded rect and the table for its corners, if one fits
struct rounded_shape {
	int32_t x;
	int32_t y;
	int32_t right;
	int32_t bottom;
	int32_t radius;
	const struct corner_table *corners;
};

static uint32_t shape_corner(
	const struct rounded_shape *shape, int32_t i, int32_t j) {
	if (shape->corners != NULL) {
		return corner_table_at(shape->corners, i, j);
	}
	double r = (double)shape->radius;
	return corner_coverage(i, j, r, r, r);
}

// j counts rows inward from the top or bottom edge
static void blend_corner_row(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, const struct rounded_shape *shape,
	int32_t y, int32_t j, bool right_side, uint32_t color) {
	if (y < clip->y0 || y >= clip->y1) {
		return;
	}
	int32_t x0 = right_side ? shape->right - shape->radius : shape->x;
	int32_t start = x0 < clip->x0 ? clip->x0 : x0;
	int32_t end =
		x0 + shape->radius > clip->x1 ? clip->x1 : x0 + shape->radius;

	uint32_t *row = buffer->data + (size_t)y * buffer->width;
	for (int32_t x = start; x < end; x++) {
		int32_t i = right_side ? shape->right - 1 - x : x - x0;
		row[x] = blend(row[x], color, shape_corner(shape, i, j));
	}
}

void matuwall_draw_rounded_rect(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color) {
	// A transparent fill is a no-op, e.g. a panel background alpha of 0
	if (width <= 0 || height <= 0 || (color >> 24) == 0) {
		return;
	}

	struct corner_table table;
	struct rounded_shape shape = {
		.x = x,
		.y = y,
		.right = x + width,
		.bottom = y + height,
		.radius = clamp_radius(radius, width, height),
	};
	shape.corners = corner_table_fill(&table, shape.radius) ? &table : NULL;
	radius = shape.radius;

	// Straight middle band: no curvature, so no per-pixel distance work
	for (int32_t row = y + radius; row < shape.bottom - radius; row++) {
		blend_span(buffer, clip, row, x, shape.right, color);
	}

	for (int32_t offset = 0; offset < radius; offset++) {
		int32_t top = y + offset;
		int32_t bot = shape.bottom - 1 - offset;

		blend_span(buffer, clip, top, x + radius, shape.right - radius,
			color);
		blend_span(buffer, clip, bot, x + radius, shape.right - radius,
			color);

		blend_corner_row(
			buffer, clip, &shape, top, offset, false, color);
		blend_corner_row(
			buffer, clip, &shape, top, offset, true, color);
		blend_corner_row(
			buffer, clip, &shape, bot, offset, false, color);
		blend_corner_row(
			buffer, clip, &shape, bot, offset, true, color);
	}
}

static void store_span(uint32_t *row, int32_t x0, int32_t x1, uint32_t color) {
	for (int32_t x = x0; x < x1; x++) {
		row[x] = color;
	}
}

// over 0 a blend is the coverage-scaled color, so each pixel is one store
static void store_corner(uint32_t *row, const struct rounded_shape *shape,
	int32_t x0, int32_t x1, int32_t j, bool right_side,
	const uint32_t *inks) {
	int32_t edge = right_side ? shape->right - shape->radius : shape->x;
	int32_t start = edge > x0 ? edge : x0;
	int32_t end = edge + shape->radius < x1 ? edge + shape->radius : x1;
	for (int32_t x = start; x < end; x++) {
		int32_t i = right_side ? shape->right - 1 - x : x - edge;
		row[x] = inks[shape_corner(shape, i, j)];
	}
}

static void store_row(uint32_t *row, const struct rounded_shape *shape,
	int32_t x0, int32_t x1, int32_t y, uint32_t color,
	const uint32_t *inks) {
	int32_t radius = shape->radius;
	int32_t j = -1;
	if (y < shape->y + radius) {
		j = y - shape->y;
	} else if (y >= shape->bottom - radius) {
		j = shape->bottom - 1 - y;
	}
	if (j < 0) {
		store_span(row, x0, x1, color);
		return;
	}
	store_corner(row, shape, x0, x1, j, false, inks);
	int32_t mid0 = shape->x + radius > x0 ? shape->x + radius : x0;
	int32_t mid1 = shape->right - radius < x1 ? shape->right - radius : x1;
	store_span(row, mid0, mid1, color);
	store_corner(row, shape, x0, x1, j, true, inks);
}

void matuwall_draw_rounded_replace(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color) {
	struct matuwall_clip clipped = *clip;
	if (!clip_to_buffer(&clipped, buffer)) {
		return;
	}
	if (width <= 0 || height <= 0 || (color >> 24) == 0) {
		clear_clipped(buffer, &clipped, 0);
		return;
	}

	struct corner_table table;
	struct rounded_shape shape = {
		.x = x,
		.y = y,
		.right = x + width,
		.bottom = y + height,
		.radius = clamp_radius(radius, width, height),
	};
	shape.corners = corner_table_fill(&table, shape.radius) ? &table : NULL;
	uint32_t inks[COVERAGE_MAX + 1];
	for (uint32_t c = 0; c <= COVERAGE_MAX; c++) {
		inks[c] = ink_at(color, c).src;
	}

	// a fresh mapping is already zero, like clear_clipped skips it
	bool zeroed = buffer->fresh;
	int32_t x0 = x > clipped.x0 ? x : clipped.x0;
	int32_t x1 = shape.right < clipped.x1 ? shape.right : clipped.x1;
	if (x0 > x1) {
		x0 = x1 = clipped.x1;
	}
	for (int32_t py = clipped.y0; py < clipped.y1; py++) {
		uint32_t *row = buffer->data + (size_t)py * buffer->width;
		bool inside = py >= y && py < shape.bottom;
		int32_t lead = inside ? x0 : clipped.x1;
		if (!zeroed) {
			store_span(row, clipped.x0, lead, 0);
		}
		if (!inside) {
			continue;
		}
		store_row(row, &shape, x0, x1, py, color, inks);
		if (!zeroed) {
			store_span(row, x1, clipped.x1, 0);
		}
	}
}

// --- ring ---

void matuwall_draw_rounded_ring(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
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
