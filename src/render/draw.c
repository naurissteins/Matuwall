#include "render/draw.h"

#include <math.h>
#include <stdbool.h>

#include "render/coverage.h"

void matuwall_draw_clear_clipped(struct matuwall_buffer *buffer,
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

static void blend_corner_row(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t y, int32_t x0, int32_t x1,
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

// --- shadow ---

// Wider falloffs skip the side-band tables and stay per pixel
#define SHADOW_BAND_MAX 256
// larger corner squares skip the corner table and stay per pixel
#define SHADOW_CORNER_MAX 128

struct shadow_shape {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t radius;
	int32_t spread;
	uint32_t color;
	int32_t left;
	int32_t right;
};

static uint32_t shadow_coverage(
	const struct shadow_shape *shape, int32_t px, int32_t py) {
	double distance = rounded_rect_distance(px, py, shape->x, shape->y,
		shape->width, shape->height, shape->radius);
	if (distance < 0.0 || distance >= shape->spread) {
		return 0;
	}
	double strength = 1.0 - distance / shape->spread;
	return (uint32_t)(strength * strength * COVERAGE_MAX + 0.5);
}

// Exact falloff per pixel, needed wherever the curve bends
static void shadow_span(uint32_t *row, const struct shadow_shape *shape,
	int32_t py, int32_t x0, int32_t x1) {
	x0 = x0 < shape->left ? shape->left : x0;
	x1 = x1 > shape->right ? shape->right : x1;
	for (int32_t px = x0; px < x1; px++) {
		row[px] = blend(
			row[px], shape->color, shadow_coverage(shape, px, py));
	}
}

// Beside a straight edge the falloff depends on one axis only
static void shadow_band(uint32_t *row, const struct shadow_shape *shape,
	const struct ink *band, int32_t start) {
	int32_t x0 = start < shape->left ? shape->left : start;
	int32_t x1 = start + shape->spread > shape->right
			     ? shape->right
			     : start + shape->spread;
	for (int32_t px = x0; px < x1; px++) {
		row[px] = blend_ink(row[px], band[px - start]);
	}
}

static void shadow_run(uint32_t *row, const struct shadow_shape *shape,
	struct ink ink, int32_t x0, int32_t x1) {
	x0 = x0 < shape->left ? shape->left : x0;
	x1 = x1 > shape->right ? shape->right : x1;
	for (int32_t px = x0; px < x1; px++) {
		row[px] = blend_ink(row[px], ink);
	}
}

// one top-left table serves all four mirrored corners; exact because each
// corner feeds the distance math the same half-integer offsets
struct shadow_corner {
	int32_t size;
	uint8_t coverage[SHADOW_CORNER_MAX * SHADOW_CORNER_MAX];
	struct ink inks[COVERAGE_MAX + 1];
};

static void shadow_corner_fill(
	struct shadow_corner *corner, const struct shadow_shape *shape) {
	int32_t size = shape->spread + shape->radius;
	corner->size = size;
	for (int32_t j = 0; j < size; j++) {
		uint8_t *row = corner->coverage + (size_t)j * (size_t)size;
		for (int32_t i = 0; i < size; i++) {
			row[i] = (uint8_t)shadow_coverage(shape,
				shape->x - shape->spread + i,
				shape->y - shape->spread + j);
		}
	}
	for (uint32_t c = 0; c <= COVERAGE_MAX; c++) {
		corner->inks[c] = ink_at(shape->color, c);
	}
}

// row j counts inward from the outer edge, mirrored spans count from x1
static void shadow_corner_span(uint32_t *row, const struct shadow_shape *shape,
	const struct shadow_corner *corner, int32_t j, int32_t x0, int32_t x1,
	bool mirrored) {
	int32_t start = x0 < shape->left ? shape->left : x0;
	int32_t end = x1 > shape->right ? shape->right : x1;
	const uint8_t *coverage =
		corner->coverage + (size_t)j * (size_t)corner->size;
	for (int32_t px = start; px < end; px++) {
		uint8_t c = coverage[mirrored ? x1 - 1 - px : px - x0];
		if (c != 0) {
			row[px] = blend_ink(row[px], corner->inks[c]);
		}
	}
}

void matuwall_draw_rounded_shadow(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t shadow_width, uint32_t color) {
	if (width <= 0 || height <= 0 || shadow_width <= 0 ||
		(color >> 24) == 0) {
		return;
	}
	radius = clamp_radius(radius, width, height);

	struct shadow_shape shape = {
		.x = x,
		.y = y,
		.width = width,
		.height = height,
		.radius = radius,
		.spread = shadow_width,
		.color = color,
		.left = x - shadow_width < clip->x0 ? clip->x0
						    : x - shadow_width,
		.right = x + width + shadow_width > clip->x1
				 ? clip->x1
				 : x + width + shadow_width,
	};
	int32_t top = y - shadow_width < clip->y0 ? clip->y0 : y - shadow_width;
	int32_t bottom = y + height + shadow_width > clip->y1
				 ? clip->y1
				 : y + height + shadow_width;

	// Side bands exist only when straight rows separate the corners
	struct ink bands[2][SHADOW_BAND_MAX];
	bool banded = shadow_width <= SHADOW_BAND_MAX && height > radius * 2;
	for (int32_t i = 0; banded && i < shadow_width; i++) {
		int32_t mid = y + height / 2;
		bands[0][i] = ink_at(color,
			shadow_coverage(&shape, x - shadow_width + i, mid));
		bands[1][i] = ink_at(
			color, shadow_coverage(&shape, x + width + i, mid));
	}
	struct shadow_corner corner;
	bool cornered = shadow_width + radius <= SHADOW_CORNER_MAX;
	if (cornered) {
		shadow_corner_fill(&corner, &shape);
	}

	for (int32_t py = top; py < bottom; py++) {
		uint32_t *row = buffer->data + (size_t)py * buffer->width;
		if (py >= y + radius && py < y + height - radius) {
			if (banded) {
				shadow_band(row, &shape, bands[0],
					x - shadow_width);
				shadow_band(row, &shape, bands[1], x + width);
			} else {
				shadow_span(
					row, &shape, py, x - shadow_width, x);
				shadow_span(row, &shape, py, x + width,
					x + width + shadow_width);
			}
			continue;
		}

		if (cornered) {
			bool upper = py < y + radius;
			int32_t j = upper ? py - (y - shadow_width)
					  : y + height + shadow_width - 1 - py;
			shadow_corner_span(row, &shape, &corner, j,
				x - shadow_width, x + radius, false);
			shadow_corner_span(row, &shape, &corner, j,
				x + width - radius, x + width + shadow_width,
				true);
		} else {
			shadow_span(
				row, &shape, py, x - shadow_width, x + radius);
			shadow_span(row, &shape, py, x + width - radius,
				x + width + shadow_width);
		}
		if (py < y || py >= y + height) {
			uint32_t coverage =
				shadow_coverage(&shape, x + radius, py);
			if (coverage > 0) {
				shadow_run(row, &shape, ink_at(color, coverage),
					x + radius, x + width - radius);
			}
		}
	}
}

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

void matuwall_draw_rounded_rect(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, uint32_t color) {
	// A transparent fill is a no-op, e.g. a panel background alpha of 0
	if (width <= 0 || height <= 0 || (color >> 24) == 0) {
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
