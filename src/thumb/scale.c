#include "thumb/scale.h"

#include <stdlib.h>

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

struct matuwall_span matuwall_axis_span(
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
	return (struct matuwall_span){.start = start, .count = end - start};
}

// --- streamed rows ---

uint64_t matuwall_row_scaler_bytes(uint32_t out_w) {
	return (uint64_t)out_w *
	       (3 * sizeof(uint64_t) + sizeof(struct matuwall_span));
}

bool matuwall_row_scaler_init(struct matuwall_row_scaler *scaler,
	uint32_t src_w, uint32_t src_h, uint32_t out_w, uint32_t out_h,
	uint32_t *pixels) {
	*scaler = (struct matuwall_row_scaler){0};
	if (pixels == NULL || !matuwall_image_dimensions_ok(src_w, src_h) ||
		!matuwall_image_dimensions_ok(out_w, out_h)) {
		return false;
	}
	uint32_t crop_x;
	uint32_t crop_w;
	matuwall_cover_crop(src_w, src_h, out_w, out_h, &crop_x,
		&scaler->crop_y, &crop_w, &scaler->crop_h);
	scaler->columns = malloc((size_t)out_w * sizeof(*scaler->columns));
	scaler->sums = malloc((size_t)out_w * 3 * sizeof(*scaler->sums));
	if (scaler->columns == NULL || scaler->sums == NULL) {
		matuwall_row_scaler_finish(scaler);
		return false;
	}
	// column spans never vary by row, so they are derived once
	for (uint32_t ox = 0; ox < out_w; ox++) {
		scaler->columns[ox] =
			matuwall_axis_span(crop_x, crop_w, out_w, ox);
	}
	scaler->pixels = pixels;
	scaler->out_w = out_w;
	scaler->out_h = out_h;
	scaler->rows =
		matuwall_axis_span(scaler->crop_y, scaler->crop_h, out_h, 0);
	return true;
}

// one column's R, G, B over a source row, the span fits 32-bit sums
static inline void sum_span(const uint8_t *row, struct matuwall_span span,
	uint32_t *r, uint32_t *g, uint32_t *b) {
	const uint8_t *p = row + (size_t)span.start * 3;
	// near 2:1 downscales are mostly two-pixel spans
	if (span.count == 2) {
		*r = (uint32_t)p[0] + p[3];
		*g = (uint32_t)p[1] + p[4];
		*b = (uint32_t)p[2] + p[5];
		return;
	}
	const uint8_t *end = p + (size_t)span.count * 3;
	uint32_t rs = 0;
	uint32_t gs = 0;
	uint32_t bs = 0;
	for (; p < end; p += 3) {
		rs += p[0];
		gs += p[1];
		bs += p[2];
	}
	*r = rs;
	*g = gs;
	*b = bs;
}

// sums hold R, G, B per column; the first row of a box stores rather than
// adds, so sums never need clearing
static void accumulate_row(const uint8_t *row, uint64_t *sums,
	const struct matuwall_span *columns, uint32_t out_w, bool first) {
	uint32_t r;
	uint32_t g;
	uint32_t b;
	if (first) {
		for (uint32_t ox = 0; ox < out_w; ox++) {
			uint64_t *sum = sums + (size_t)ox * 3;
			sum_span(row, columns[ox], &r, &g, &b);
			sum[0] = r;
			sum[1] = g;
			sum[2] = b;
		}
		return;
	}
	for (uint32_t ox = 0; ox < out_w; ox++) {
		uint64_t *sum = sums + (size_t)ox * 3;
		sum_span(row, columns[ox], &r, &g, &b);
		sum[0] += r;
		sum[1] += g;
		sum[2] += b;
	}
}

// same quotient as a 64-bit divide; 32-bit division is much cheaper and
// exact whenever the box sum fits, which is every real box
static inline uint32_t box_average(uint64_t sum, uint64_t count) {
	if (count <= UINT32_MAX / 255) {
		return (uint32_t)sum / (uint32_t)count;
	}
	return (uint32_t)(sum / count);
}

// one-row box averages straight from the source row
static void write_single_row(uint32_t *dst, const uint8_t *row,
	const struct matuwall_span *columns, uint32_t out_w) {
	for (uint32_t ox = 0; ox < out_w; ox++) {
		uint32_t count = columns[ox].count;
		if (count == 1) {
			const uint8_t *p = row + (size_t)columns[ox].start * 3;
			dst[ox] = 0xff000000u | (uint32_t)p[0] << 16 |
				  (uint32_t)p[1] << 8 | (uint32_t)p[2];
			continue;
		}
		uint32_t r;
		uint32_t g;
		uint32_t b;
		sum_span(row, columns[ox], &r, &g, &b);
		dst[ox] = 0xff000000u | box_average(r, count) << 16 |
			  box_average(g, count) << 8 | box_average(b, count);
	}
}

static void write_row(uint32_t *dst, const uint64_t *sums,
	const struct matuwall_span *columns, uint32_t out_w,
	uint32_t source_rows) {
	for (uint32_t ox = 0; ox < out_w; ox++) {
		uint64_t count = (uint64_t)columns[ox].count * source_rows;
		const uint64_t *sum = sums + (size_t)ox * 3;
		dst[ox] = 0xff000000u | box_average(sum[0], count) << 16 |
			  box_average(sum[1], count) << 8 |
			  box_average(sum[2], count);
	}
}

void matuwall_row_scaler_push(
	struct matuwall_row_scaler *scaler, uint32_t y, const uint8_t *rgb) {
	uint32_t out_w = scaler->out_w;
	// upscaling reuses one source row for several output rows
	while (scaler->next_row < scaler->out_h && y >= scaler->rows.start) {
		uint32_t *dst =
			scaler->pixels + (size_t)scaler->next_row * out_w;
		if (scaler->rows.count == 1) {
			write_single_row(dst, rgb, scaler->columns, out_w);
		} else {
			accumulate_row(rgb, scaler->sums, scaler->columns,
				out_w, y == scaler->rows.start);
			if (y + 1 < scaler->rows.start + scaler->rows.count) {
				return;
			}
			write_row(dst, scaler->sums, scaler->columns, out_w,
				scaler->rows.count);
		}
		scaler->next_row++;
		if (scaler->next_row < scaler->out_h) {
			scaler->rows = matuwall_axis_span(scaler->crop_y,
				scaler->crop_h, scaler->out_h,
				scaler->next_row);
		}
	}
}

bool matuwall_row_scaler_done(const struct matuwall_row_scaler *scaler) {
	return scaler->next_row == scaler->out_h;
}

void matuwall_row_scaler_finish(struct matuwall_row_scaler *scaler) {
	free(scaler->columns);
	free(scaler->sums);
	scaler->columns = NULL;
	scaler->sums = NULL;
}
