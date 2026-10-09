#include "thumb/decode/format.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdlib.h>

#include <png.h>

#include "thumb/scale.h"

struct png_decode_buffers {
	// one RGB row, or the whole image when interlaced
	png_bytep rows;
	struct matuwall_row_scaler scaler;
};

static void png_buffers_free(struct png_decode_buffers *buffers) {
	free(buffers->rows);
	matuwall_row_scaler_finish(&buffers->scaler);
	*buffers = (struct png_decode_buffers){0};
}

// every format lands in packed 8-bit RGB
static void normalize_png(png_structp png, png_infop info) {
	int bit_depth = png_get_bit_depth(png, info);
	int color_type = png_get_color_type(png, info);
	if (bit_depth == 16) {
		png_set_strip_16(png);
	}
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
		png_set_expand_gray_1_2_4_to_8(png);
	}
	if (png_get_valid(png, info, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY ||
		color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png);
	}
	png_set_strip_alpha(png);
}

// an interlaced row is final only in the last pass, so every row is kept
static bool decode_png_rows(png_structp png, struct matuwall_image *img,
	uint32_t width, uint32_t height, int passes,
	struct png_decode_buffers *buffers, const struct decode_job *job) {
	bool interlaced = passes > 1;
	size_t row_bytes = (size_t)width * 3;
	uint32_t kept = interlaced ? height : 1;
	if ((interlaced &&
		    !decode_dimensions_ok(width, height, job->purpose)) ||
		!reserve(job,
			target_bytes(job) + (uint64_t)row_bytes * kept +
				matuwall_row_scaler_bytes(job->target_w))) {
		return false;
	}
	bool allocated = alloc_target(img, job);
	buffers->rows = malloc(row_bytes * kept);
	if (!allocated || buffers->rows == NULL ||
		!matuwall_row_scaler_init(&buffers->scaler, width, height,
			job->target_w, job->target_h, img->pixels)) {
		png_longjmp(png, 1);
	}

	for (int pass = 0; pass < passes; pass++) {
		for (uint32_t y = 0; y < height; y++) {
			if (stop_requested(job->stop)) {
				png_longjmp(png, 1);
			}
			png_bytep row = buffers->rows +
					(interlaced ? y * row_bytes : 0);
			png_read_row(png, row, NULL);
			if (pass == passes - 1) {
				matuwall_row_scaler_push(
					&buffers->scaler, y, row);
			}
		}
	}
	png_read_end(png, NULL);
	return true;
}

// libpng prints through its own handlers; keep that off stderr as well
static void png_on_error(png_structp png, png_const_charp message) {
	(void)message;
	png_longjmp(png, 1);
}

static void png_on_warning(png_structp png, png_const_charp message) {
	(void)png;
	(void)message;
}

bool matuwall_decode_png(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job) {
	png_structp png = png_create_read_struct(
		PNG_LIBPNG_VER_STRING, NULL, png_on_error, png_on_warning);
	png_infop info = png != NULL ? png_create_info_struct(png) : NULL;
	struct png_decode_buffers *buffers = calloc(1, sizeof(*buffers));
	if (png == NULL || info == NULL || buffers == NULL) {
		free(buffers);
		if (png != NULL) {
			png_destroy_read_struct(
				&png, info != NULL ? &info : NULL, NULL);
		}
		return false;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_buffers_free(buffers);
		free(buffers);
		matuwall_image_free(img);
		png_destroy_read_struct(&png, &info, NULL);
		return false;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	uint32_t width = png_get_image_width(png, info);
	uint32_t height = png_get_image_height(png, info);
	if (!matuwall_image_dimensions_ok(width, height)) {
		free(buffers);
		png_destroy_read_struct(&png, &info, NULL);
		return false;
	}

	normalize_png(png, info);
	int passes = 1;
	if (png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) {
		passes = png_set_interlace_handling(png);
	}
	png_read_update_info(png, info);
	if (png_get_rowbytes(png, info) != (size_t)width * 3) {
		png_longjmp(png, 1);
	}

	bool ok =
		decode_png_rows(png, img, width, height, passes, buffers, job);
	png_buffers_free(buffers);
	free(buffers);
	png_destroy_read_struct(&png, &info, NULL);
	return ok;
}
