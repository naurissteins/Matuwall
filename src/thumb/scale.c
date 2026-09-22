#include "thumb/scale.h"

#include <stdlib.h>

static bool stop_requested(const atomic_bool *stop) {
	return stop != NULL && atomic_load_explicit(stop, memory_order_relaxed);
}

void matuwall_cover_crop(uint32_t src_w, uint32_t src_h, uint32_t out_w,
	uint32_t out_h, uint32_t *cx, uint32_t *cy, uint32_t *cw,
	uint32_t *ch) {
	if ((uint64_t)src_w * out_h > (uint64_t)out_w * src_h) {
		// Source is wider: crop the sides
		*ch = src_h;
		*cw = (uint32_t)((uint64_t)out_w * src_h / out_h);
		*cx = (src_w - *cw) / 2;
		*cy = 0;
	} else {
		// Source is taller: crop top and bottom
		*cw = src_w;
		*ch = (uint32_t)((uint64_t)out_h * src_w / out_w);
		*cx = 0;
		*cy = (src_h - *ch) / 2;
	}
	if (*cw == 0) {
		*cw = 1;
	}
	if (*ch == 0) {
		*ch = 1;
	}
}

static uint32_t average_box(const struct matuwall_image *src, uint32_t sx0,
	uint32_t sx1, uint32_t sy0, uint32_t sy1) {
	uint64_t b = 0;
	uint64_t g = 0;
	uint64_t r = 0;
	for (uint32_t sy = sy0; sy < sy1; sy++) {
		const uint32_t *row = src->pixels + (size_t)sy * src->width;
		for (uint32_t sx = sx0; sx < sx1; sx++) {
			uint32_t p = row[sx];
			b += p & 0xff;
			g += (p >> 8) & 0xff;
			r += (p >> 16) & 0xff;
		}
	}
	uint64_t count = (uint64_t)(sx1 - sx0) * (sy1 - sy0);
	uint32_t br = (uint32_t)(b / count);
	uint32_t gr = (uint32_t)(g / count);
	uint32_t rr = (uint32_t)(r / count);
	return 0xff000000u | rr << 16 | gr << 8 | br;
}

// One source span per output pixel along an axis
struct span {
	uint32_t start;
	uint32_t count;
};

static struct span axis_span(
	uint32_t origin, uint32_t extent, uint32_t output, uint32_t index) {
	uint32_t start = origin + (uint32_t)((uint64_t)index * extent / output);
	uint32_t end =
		origin + (uint32_t)((uint64_t)(index + 1) * extent / output);
	if (end <= start) {
		end = start + 1;
	}
	if (end > origin + extent) {
		end = origin + extent;
	}
	return (struct span){.start = start, .count = end - start};
}

static void scale_row(const struct matuwall_image *src,
	const struct span *columns, bool single_column, struct span rows,
	uint32_t *dst_row, uint32_t out_w) {
	// A 1x1 box is the source pixel; averaging it changes nothing
	if (single_column && rows.count == 1) {
		const uint32_t *src_row =
			src->pixels + (size_t)rows.start * src->width;
		for (uint32_t ox = 0; ox < out_w; ox++) {
			dst_row[ox] =
				0xff000000u |
				(src_row[columns[ox].start] & 0x00ffffffu);
		}
		return;
	}
	for (uint32_t ox = 0; ox < out_w; ox++) {
		dst_row[ox] = average_box(src, columns[ox].start,
			columns[ox].start + columns[ox].count, rows.start,
			rows.start + rows.count);
	}
}

bool matuwall_scale_cover(const struct matuwall_image *src, uint32_t out_w,
	uint32_t out_h, struct matuwall_image *out, const atomic_bool *stop) {
	*out = (struct matuwall_image){0};
	if (src->pixels == NULL ||
		!matuwall_image_dimensions_ok(src->width, src->height) ||
		!matuwall_image_dimensions_ok(out_w, out_h) ||
		(size_t)out_w > SIZE_MAX / sizeof(uint32_t) / out_h ||
		stop_requested(stop)) {
		return false;
	}
	size_t pixel_count = (size_t)out_w * out_h;

	uint32_t cx;
	uint32_t cy;
	uint32_t cw;
	uint32_t ch;
	matuwall_cover_crop(
		src->width, src->height, out_w, out_h, &cx, &cy, &cw, &ch);

	// Column spans never vary by row, so they are derived once
	struct span *columns = malloc((size_t)out_w * sizeof(*columns));
	uint32_t *pixels = malloc(pixel_count * sizeof(uint32_t));
	if (columns == NULL || pixels == NULL) {
		free(columns);
		free(pixels);
		return false;
	}
	bool single_column = true;
	for (uint32_t ox = 0; ox < out_w; ox++) {
		columns[ox] = axis_span(cx, cw, out_w, ox);
		single_column = single_column && columns[ox].count == 1;
	}

	for (uint32_t oy = 0; oy < out_h; oy++) {
		if (stop_requested(stop)) {
			free(columns);
			free(pixels);
			return false;
		}
		scale_row(src, columns, single_column,
			axis_span(cy, ch, out_h, oy),
			pixels + (size_t)oy * out_w, out_w);
	}
	free(columns);

	out->width = out_w;
	out->height = out_h;
	out->pixels = pixels;
	return true;
}
