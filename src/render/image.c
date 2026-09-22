#include "render/image.h"

#include <stdbool.h>
#include <string.h>

#include "render/coverage.h"
#include "render/sample.h"

#define BILINEAR_AXIS_CAPACITY 2048

// fixed point source stepping keeps the full-screen blit off the divider
#define COVER_SHIFT 16

void matuwall_draw_image_cover_clipped(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, const uint32_t *src, uint32_t src_w,
	uint32_t src_h) {
	if (src == NULL || src_w == 0 || src_h == 0 || buffer->width == 0 ||
		buffer->height == 0) {
		return;
	}
	struct matuwall_clip clipped = *clip;
	if (!clip_to_buffer(&clipped, buffer)) {
		return;
	}

	// The scaler already covers the buffer exactly in the common case
	if (src_w == buffer->width && src_h == buffer->height) {
		size_t count = (size_t)(clipped.x1 - clipped.x0);
		for (int32_t y = clipped.y0; y < clipped.y1; y++) {
			memcpy(buffer->data + (size_t)y * buffer->width +
					clipped.x0,
				src + (size_t)y * src_w + clipped.x0,
				count * sizeof(uint32_t));
		}
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

	for (int32_t y = clipped.y0; y < clipped.y1; y++) {
		uint32_t sy = off_y + (uint32_t)((uint64_t)(uint32_t)y *
						 crop_h / buffer->height);
		if (sy >= src_h) {
			sy = src_h - 1;
		}
		const uint32_t *src_row = src + (size_t)sy * src_w;
		uint32_t *dst_row =
			buffer->data + (size_t)(uint32_t)y * buffer->width;

		uint64_t pos = (uint64_t)(uint32_t)clipped.x0 * step_x;
		for (int32_t x = clipped.x0; x < clipped.x1; x++) {
			uint32_t sx = off_x + (uint32_t)(pos >> COVER_SHIFT);
			pos += step_x;
			if (sx >= src_w) {
				sx = src_w - 1;
			}
			dst_row[x] = src_row[sx];
		}
	}
}

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

static void draw_image_edge(uint32_t *dst, struct image_row *row, int32_t x0,
	int32_t x1, int32_t py, int32_t shape_x, int32_t shape_y,
	int32_t shape_width, int32_t shape_height, int32_t radius) {
	for (int32_t px = x0; px < x1; px++) {
		uint32_t pixel = image_row_next(row);
		uint32_t coverage = rect_coverage(px, py, shape_x, shape_y,
			shape_width, shape_height, radius);
		if (coverage > 0) {
			dst[px] = blend(dst[px], pixel, coverage);
		}
	}
}

static void draw_image_span(uint32_t *dst, struct image_row *row,
	const struct matuwall_bilinear_axis *axes, int32_t x0, int32_t x1) {
	size_t count = (size_t)(x1 - x0);
	if (row->bilinear && axes != NULL) {
		matuwall_bilinear_row_span(
			&row->filtered, axes, dst + x0, (uint32_t)count);
		return;
	}
	if (!row->bilinear && row->nearest.step == 1 &&
		row->nearest.remainder_step == 0) {
		memcpy(dst + x0, row->nearest.pixels + row->nearest.index,
			count * sizeof(uint32_t));
		row->nearest.index += (uint32_t)count;
		return;
	}
	for (int32_t px = x0; px < x1; px++) {
		dst[px] = image_row_next(row);
	}
}

static void draw_image_row(struct matuwall_buffer *buffer,
	struct image_row *row, const struct matuwall_bilinear_axis *axes,
	int32_t left, int32_t right, int32_t py, int32_t shape_x,
	int32_t shape_y, int32_t shape_width, int32_t shape_height,
	int32_t radius) {
	int32_t full_left = shape_x + radius;
	int32_t full_right = shape_x + shape_width - radius;
	if (radius == 0 || (py >= shape_y + radius &&
				   py < shape_y + shape_height - radius)) {
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
	draw_image_edge(dst, row, left, full_left, py, shape_x, shape_y,
		shape_width, shape_height, radius);
	const struct matuwall_bilinear_axis *span_axes =
		axes != NULL ? axes + (full_left - left) : NULL;
	draw_image_span(dst, row, span_axes, full_left, full_right);
	draw_image_edge(dst, row, full_right, right, py, shape_x, shape_y,
		shape_width, shape_height, radius);
}

static void draw_image_rounded(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h, bool bilinear) {
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
	if (left >= right || top >= bottom) {
		return;
	}
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
		draw_image_row(buffer, &row, prepared, left, right, py, inner_x,
			inner_y, inner_width, inner_height, inner_radius);
	}
}

void matuwall_draw_image_rounded(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h) {
	draw_image_rounded(buffer, clip, x, y, width, height, radius, inset,
		src, src_w, src_h, false);
}

void matuwall_draw_image_rounded_bilinear(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t x, int32_t y, int32_t width,
	int32_t height, int32_t radius, int32_t inset, const uint32_t *src,
	uint32_t src_w, uint32_t src_h) {
	draw_image_rounded(buffer, clip, x, y, width, height, radius, inset,
		src, src_w, src_h, true);
}
