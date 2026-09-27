#include "render/image.h"

#include <stdbool.h>
#include <string.h>

#include "render/color.h"
#include "render/coverage.h"
#include "render/sample.h"

#define BILINEAR_AXIS_CAPACITY 2048
#define FADE_CHUNK 256

struct nearest_row {
	const uint32_t *pixels;
	uint32_t width;
	uint32_t index;
	uint32_t step;
	uint32_t remainder;
	uint32_t remainder_step;
	uint32_t divisor;
};

struct image_row {
	bool bilinear;
	struct nearest_row nearest;
	struct matuwall_bilinear_row filtered;
};

static void image_row_init(struct image_row *row,
	const struct matuwall_bilinear_sampler *sampler, const uint32_t *src,
	uint32_t src_w, uint32_t src_h, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t left, int32_t py) {
	uint32_t offset_x = (uint32_t)(left - x);
	uint32_t offset_y = (uint32_t)(py - y);
	if (sampler != NULL) {
		*row = (struct image_row){.bilinear = true};
		matuwall_bilinear_row_init(
			&row->filtered, sampler, offset_x, offset_y);
		return;
	}

	uint32_t sy = (uint32_t)((uint64_t)offset_y * src_h / (uint32_t)height);
	uint64_t numerator = (uint64_t)offset_x * src_w;
	*row = (struct image_row){
		.nearest =
			{
				.pixels = src + (size_t)sy * src_w,
				.width = src_w,
				.index =
					(uint32_t)(numerator / (uint32_t)width),
				.step = src_w / (uint32_t)width,
				.remainder =
					(uint32_t)(numerator % (uint32_t)width),
				.remainder_step = src_w % (uint32_t)width,
				.divisor = (uint32_t)width,
			},
	};
}

static uint32_t image_row_next(struct image_row *row) {
	if (row->bilinear) {
		return matuwall_bilinear_row_next(&row->filtered);
	}

	struct nearest_row *nearest = &row->nearest;
	uint32_t index = nearest->index < nearest->width ? nearest->index
							 : nearest->width - 1;
	uint32_t pixel = nearest->pixels[index];
	nearest->index += nearest->step;
	nearest->remainder += nearest->remainder_step;
	if (nearest->remainder >= nearest->divisor) {
		nearest->remainder -= nearest->divisor;
		nearest->index++;
	}
	return pixel;
}

// --- rounded edges ---

// the rounded clip a thumbnail is drawn through
struct image_shape {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t radius;
	const struct corner_table *corners;
};

// edge pixels only ever lie in one of the four corner squares
static uint32_t edge_coverage(
	const struct image_shape *shape, int32_t px, int32_t py) {
	if (shape->corners == NULL) {
		return rect_coverage(px, py, shape->x, shape->y, shape->width,
			shape->height, shape->radius);
	}
	int32_t i = px < shape->x + shape->radius
			    ? px - shape->x
			    : shape->x + shape->width - 1 - px;
	int32_t j = py < shape->y + shape->radius
			    ? py - shape->y
			    : shape->y + shape->height - 1 - py;
	return corner_table_at(shape->corners, i, j);
}

static void draw_image_edge(uint32_t *dst, struct image_row *row, int32_t x0,
	int32_t x1, int32_t py, const struct image_shape *shape,
	uint8_t opacity) {
	for (int32_t px = x0; px < x1; px++) {
		uint32_t pixel = image_row_next(row);
		uint32_t coverage =
			fade_coverage(edge_coverage(shape, px, py), opacity);
		if (coverage > 0) {
			dst[px] = blend(dst[px], pixel, coverage);
		}
	}
}

static void sample_image_span(uint32_t *out, struct image_row *row,
	const struct matuwall_bilinear_axis *axes, size_t count) {
	if (row->bilinear && axes != NULL) {
		matuwall_bilinear_row_span(
			&row->filtered, axes, out, (uint32_t)count);
		return;
	}
	if (!row->bilinear && row->nearest.step == 1 &&
		row->nearest.remainder_step == 0) {
		memcpy(out, row->nearest.pixels + row->nearest.index,
			count * sizeof(uint32_t));
		row->nearest.index += (uint32_t)count;
		return;
	}
	for (size_t i = 0; i < count; i++) {
		out[i] = image_row_next(row);
	}
}

static void draw_image_span(uint32_t *dst, struct image_row *row,
	const struct matuwall_bilinear_axis *axes, int32_t x0, int32_t x1,
	uint8_t opacity) {
	size_t count = (size_t)(x1 - x0);
	if (opacity == MATUWALL_OPAQUE) {
		sample_image_span(dst + x0, row, axes, count);
		return;
	}

	// sample through the fast paths, then blend each chunk over dst
	uint32_t chunk[FADE_CHUNK];
	for (size_t done = 0; done < count;) {
		size_t n =
			count - done < FADE_CHUNK ? count - done : FADE_CHUNK;
		sample_image_span(
			chunk, row, axes != NULL ? axes + done : NULL, n);
		uint32_t *out = dst + x0 + done;
		for (size_t i = 0; i < n; i++) {
			out[i] = blend_opaque(out[i], chunk[i], opacity);
		}
		done += n;
	}
}

static void draw_image_row(struct matuwall_buffer *buffer,
	struct image_row *row, const struct matuwall_bilinear_axis *axes,
	int32_t left, int32_t right, int32_t py,
	const struct image_shape *shape, uint8_t opacity) {
	int32_t radius = shape->radius;
	int32_t full_left = shape->x + radius;
	int32_t full_right = shape->x + shape->width - radius;
	if (radius == 0 || (py >= shape->y + radius &&
				   py < shape->y + shape->height - radius)) {
		full_left = left;
		full_right = right;
	}
	if (full_left < left) {
		full_left = left;
	}
	if (full_left > right) {
		full_left = right;
	}
	if (full_right < left) {
		full_right = left;
	}
	if (full_right > right) {
		full_right = right;
	}

	uint32_t *dst = buffer->data + (size_t)py * buffer->width;
	draw_image_edge(dst, row, left, full_left, py, shape, opacity);
	const struct matuwall_bilinear_axis *span_axes =
		axes != NULL ? axes + (full_left - left) : NULL;
	draw_image_span(dst, row, span_axes, full_left, full_right, opacity);
	draw_image_edge(dst, row, full_right, right, py, shape, opacity);
}

static void draw_image_rounded(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h, bool bilinear, uint8_t opacity) {
	if (width <= 0 || height <= 0 || inset < 0 || inset > (width - 1) / 2 ||
		inset > (height - 1) / 2 || src == NULL || src_w == 0 ||
		src_h == 0 || opacity == 0) {
		return;
	}

	radius = clamp_radius(radius, width, height);
	struct image_shape shape = {
		.x = x + inset,
		.y = y + inset,
		.width = width - inset * 2,
		.height = height - inset * 2,
		.radius = radius > inset ? radius - inset : 0,
	};

	int32_t top = shape.y < clip->y0 ? clip->y0 : shape.y;
	int32_t bottom = shape.y + shape.height > clip->y1
				 ? clip->y1
				 : shape.y + shape.height;
	int32_t left = shape.x < clip->x0 ? clip->x0 : shape.x;
	int32_t right = shape.x + shape.width > clip->x1
				? clip->x1
				: shape.x + shape.width;
	if (left >= right || top >= bottom) {
		return;
	}
	shape.corners = matuwall_corner_table(shape.radius);
	struct matuwall_bilinear_sampler sampler;
	if (bilinear && !matuwall_bilinear_sampler_init(&sampler, src, src_w,
				src_h, (uint32_t)width, (uint32_t)height)) {
		return;
	}
	struct matuwall_bilinear_axis axes[BILINEAR_AXIS_CAPACITY];
	const struct matuwall_bilinear_axis *prepared = NULL;
	if (bilinear && right - left <= BILINEAR_AXIS_CAPACITY) {
		matuwall_bilinear_axes_init(&sampler, (uint32_t)(left - x),
			axes, (uint32_t)(right - left));
		prepared = axes;
	}

	for (int32_t py = top; py < bottom; py++) {
		struct image_row row;
		image_row_init(&row, bilinear ? &sampler : NULL, src, src_w,
			src_h, x, y, width, height, left, py);
		draw_image_row(buffer, &row, prepared, left, right, py, &shape,
			opacity);
	}
}

void matuwall_draw_image_rounded(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h, uint8_t opacity) {
	draw_image_rounded(buffer, clip, x, y, width, height, radius, inset,
		src, src_w, src_h, false, opacity);
}

void matuwall_draw_image_rounded_bilinear(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h, uint8_t opacity) {
	draw_image_rounded(buffer, clip, x, y, width, height, radius, inset,
		src, src_w, src_h, true, opacity);
}
