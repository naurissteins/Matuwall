#include "thumb/decode/format.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdlib.h>

#include <png.h>

#include "thumb/scale.h"

struct png_decode_buffers {
	png_bytep *rows;
	png_bytep row;
	struct matuwall_row_scaler scaler;
};

static void png_buffers_free(struct png_decode_buffers *buffers) {
	free((void *)buffers->rows);
	free(buffers->row);
	matuwall_row_scaler_finish(&buffers->scaler);
	*buffers = (struct png_decode_buffers){0};
}

static void normalize_png(png_structp png, png_infop info, bool argb) {
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
	// whole image decode lands in opaque ARGB8888, streaming keeps RGB
	if (argb) {
		png_set_bgr(png);
		png_set_filler(png, 0xff, PNG_FILLER_AFTER);
	}
}

static bool decode_interlaced_png(png_structp png, struct matuwall_image *img,
	uint32_t width, uint32_t height, int passes,
	struct png_decode_buffers *buffers, const struct decode_job *job) {
	uint64_t whole = (uint64_t)width * height * sizeof(uint32_t);
	// the worker's cover scale holds its output beside the whole image
	if (!decode_dimensions_ok(width, height, job->purpose) ||
		!reserve(job,
			whole + target_bytes(job) +
				matuwall_row_scaler_bytes(job->target_w))) {
		return false;
	}
	img->width = width;
	img->height = height;
	img->pixels = calloc((size_t)width * height, sizeof(uint32_t));
	buffers->rows =
		(png_bytep *)malloc((size_t)height * sizeof(*buffers->rows));
	if (img->pixels == NULL || buffers->rows == NULL) {
		png_longjmp(png, 1);
	}
	for (uint32_t y = 0; y < height; y++) {
		buffers->rows[y] = (png_bytep)(img->pixels + (size_t)y * width);
	}
	for (int pass = 0; pass < passes; pass++) {
		for (uint32_t y = 0; y < height; y++) {
			if (stop_requested(job->stop)) {
				png_longjmp(png, 1);
			}
			png_read_row(png, buffers->rows[y], NULL);
		}
	}
	png_read_end(png, NULL);

	free((void *)buffers->rows);
	buffers->rows = NULL;
	return true;
}

static bool decode_png_rows(png_structp png, struct matuwall_image *img,
	uint32_t width, uint32_t height, struct png_decode_buffers *buffers,
	const struct decode_job *job) {
	if (!reserve(job, target_bytes(job) + (uint64_t)width * 3 +
				  matuwall_row_scaler_bytes(job->target_w))) {
		return false;
	}
	img->width = job->target_w;
	img->height = job->target_h;
	img->pixels = alloc_target(job);
	buffers->row = malloc((size_t)width * 3);
	if (img->pixels == NULL || buffers->row == NULL ||
		!matuwall_row_scaler_init(&buffers->scaler, width, height,
			job->target_w, job->target_h, img->pixels)) {
		png_longjmp(png, 1);
	}

	for (uint32_t y = 0; y < height; y++) {
		if (stop_requested(job->stop)) {
			png_longjmp(png, 1);
		}
		png_read_row(png, buffers->row, NULL);
		matuwall_row_scaler_push(&buffers->scaler, y, buffers->row);
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

	bool interlaced =
		png_get_interlace_type(png, info) != PNG_INTERLACE_NONE;
	normalize_png(png, info, interlaced);
	int passes = 1;
	if (interlaced) {
		passes = png_set_interlace_handling(png);
	}
	png_read_update_info(png, info);
	size_t pixel_bytes = interlaced ? sizeof(uint32_t) : 3;
	if (png_get_rowbytes(png, info) != (size_t)width * pixel_bytes) {
		png_longjmp(png, 1);
	}

	bool ok = interlaced ? decode_interlaced_png(png, img, width, height,
				       passes, buffers, job)
			     : decode_png_rows(
				       png, img, width, height, buffers, job);
	png_buffers_free(buffers);
	free(buffers);
	png_destroy_read_struct(&png, &info, NULL);
	return ok;
}
