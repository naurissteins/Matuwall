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

	uint32_t *pixels = malloc(pixel_count * sizeof(uint32_t));
	if (pixels == NULL) {
		return false;
	}

	for (uint32_t oy = 0; oy < out_h; oy++) {
		if (stop_requested(stop)) {
			free(pixels);
			return false;
		}
		uint32_t sy0 = cy + (uint32_t)((uint64_t)oy * ch / out_h);
		uint32_t sy1 = cy + (uint32_t)((uint64_t)(oy + 1) * ch / out_h);
		if (sy1 <= sy0) {
			sy1 = sy0 + 1;
		}
		if (sy1 > cy + ch) {
			sy1 = cy + ch;
		}

		uint32_t *dst_row = pixels + (size_t)oy * out_w;
		for (uint32_t ox = 0; ox < out_w; ox++) {
			uint32_t sx0 =
				cx + (uint32_t)((uint64_t)ox * cw / out_w);
			uint32_t sx1 = cx + (uint32_t)((uint64_t)(ox + 1) * cw /
						       out_w);
			if (sx1 <= sx0) {
				sx1 = sx0 + 1;
			}
			if (sx1 > cx + cw) {
				sx1 = cx + cw;
			}
			dst_row[ox] = average_box(src, sx0, sx1, sy0, sy1);
		}
	}

	out->width = out_w;
	out->height = out_h;
	out->pixels = pixels;
	return true;
}
