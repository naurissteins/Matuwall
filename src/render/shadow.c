#include "render/draw.h"

#include <stdbool.h>

#include "render/coverage.h"

// wider falloffs skip the side-band tables and stay per pixel
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
	uint8_t cover_opacity;
	int32_t left;
	int32_t right;
};

static uint32_t shadow_coverage(
	const struct shadow_shape *shape, int32_t px, int32_t py) {
	double distance = rounded_rect_distance(px, py, shape->x, shape->y,
		shape->width, shape->height, shape->radius);
	if (distance <= -0.5 || distance >= shape->spread) {
		return 0;
	}
	double strength = distance > 0.0 ? 1.0 - distance / shape->spread : 1.0;
	strength *= strength;
	// under the tile's edge ramp only the uncovered part may show shadow,
	// scaled so it survives the tile's own blend at its opacity
	uint32_t cover =
		distance < CURVE_SOFTEN + 0.5
			? rect_coverage(px, py, shape->x, shape->y,
				  shape->width, shape->height, shape->radius)
			: 0;
	if (cover > 0) {
		if (cover == COVERAGE_MAX) {
			return 0;
		}
		double c = (double)cover / COVERAGE_MAX;
		double alpha = (double)shape->cover_opacity / COVERAGE_MAX;
		strength *= (1.0 - c) / (1.0 - c * alpha);
	}
	return (uint32_t)(strength * COVERAGE_MAX + 0.5);
}

// exact falloff per pixel, needed wherever the curve bends
static void shadow_span(uint32_t *row, const struct shadow_shape *shape,
	int32_t py, int32_t x0, int32_t x1) {
	x0 = x0 < shape->left ? shape->left : x0;
	x1 = x1 > shape->right ? shape->right : x1;
	for (int32_t px = x0; px < x1; px++) {
		row[px] = blend(
			row[px], shape->color, shadow_coverage(shape, px, py));
	}
}

// beside a straight edge the falloff depends on one axis only
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
	int32_t height, int32_t radius, int32_t shadow_width, uint32_t color,
	uint8_t cover_opacity) {
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
		.cover_opacity = cover_opacity,
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

	// side bands exist only when straight rows separate the corners
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
