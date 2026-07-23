#include "thumb/decode.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

#define MAX_DIMENSION 16384u
#define MAX_PIXELS (1u << 26)
#define WEBP_FILE_LIMIT (64UL * 1024 * 1024)

bool sweetwall_image_dimensions_ok(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0 || width > MAX_DIMENSION ||
		height > MAX_DIMENSION) {
		return false;
	}
	return (uint64_t)width * height <= MAX_PIXELS;
}

static void force_opaque(uint32_t *pixels, size_t count) {
	for (size_t i = 0; i < count; i++) {
		pixels[i] |= 0xff000000u;
	}
}

// --- jpeg ---

struct jpeg_guard {
	struct jpeg_error_mgr base;
	jmp_buf jmp;
};

static void jpeg_on_error(j_common_ptr cinfo) {
	struct jpeg_guard *guard = (struct jpeg_guard *)cinfo->err;
	longjmp(guard->jmp, 1);
}

static bool decode_jpeg(FILE *fp, struct sweetwall_image *img) {
	struct jpeg_decompress_struct cinfo;
	struct jpeg_guard guard;
	cinfo.err = jpeg_std_error(&guard.base);
	guard.base.error_exit = jpeg_on_error;

	if (setjmp(guard.jmp)) {
		jpeg_destroy_decompress(&cinfo);
		free(img->pixels);
		img->pixels = NULL;
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, fp);
	jpeg_read_header(&cinfo, TRUE);

	if (!sweetwall_image_dimensions_ok(
		    cinfo.image_width, cinfo.image_height)) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	cinfo.out_color_space = JCS_EXT_BGRA;
	jpeg_start_decompress(&cinfo);

	uint32_t width = cinfo.output_width;
	uint32_t height = cinfo.output_height;
	img->width = width;
	img->height = height;
	img->pixels = malloc((size_t)width * height * sizeof(uint32_t));
	if (img->pixels == NULL) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	while (cinfo.output_scanline < height) {
		JSAMPROW row =
			(JSAMPROW)(img->pixels +
				   (size_t)cinfo.output_scanline * width);
		jpeg_read_scanlines(&cinfo, &row, 1);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return true;
}

// --- png ---

static void rgba_to_argb(uint32_t *pixels, size_t count) {
	for (size_t i = 0; i < count; i++) {
		uint8_t *p = (uint8_t *)&pixels[i];
		uint8_t r = p[0];
		p[0] = p[2];
		p[2] = r;
	}
}

static bool decode_png(FILE *fp, struct sweetwall_image *img) {
	png_structp png =
		png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL) {
		return false;
	}
	png_infop info = png_create_info_struct(png);
	if (info == NULL) {
		png_destroy_read_struct(&png, NULL, NULL);
		return false;
	}

	png_bytep *volatile rows = NULL;
	if (setjmp(png_jmpbuf(png))) {
		free((void *)rows);
		free(img->pixels);
		img->pixels = NULL;
		png_destroy_read_struct(&png, &info, NULL);
		return false;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	uint32_t width = png_get_image_width(png, info);
	uint32_t height = png_get_image_height(png, info);
	if (!sweetwall_image_dimensions_ok(width, height)) {
		png_destroy_read_struct(&png, &info, NULL);
		return false;
	}

	// Normalize any input to 8-bit RGBA
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
	png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
	png_read_update_info(png, info);

	img->width = width;
	img->height = height;
	img->pixels = malloc((size_t)width * height * sizeof(uint32_t));
	rows = (png_bytep *)malloc((size_t)height * sizeof(png_bytep));
	if (img->pixels == NULL || rows == NULL) {
		png_longjmp(png, 1);
	}

	for (uint32_t y = 0; y < height; y++) {
		rows[y] = (png_bytep)(img->pixels + (size_t)y * width);
	}
	png_read_image(png, rows);

	free((void *)rows);
	png_destroy_read_struct(&png, &info, NULL);
	rgba_to_argb(img->pixels, (size_t)width * height);
	force_opaque(img->pixels, (size_t)width * height);
	return true;
}

// --- webp ---

static bool decode_webp(FILE *fp, struct sweetwall_image *img) {
	if (fseek(fp, 0, SEEK_END) != 0) {
		return false;
	}
	long size = ftell(fp);
	if (size <= 0 || (unsigned long)size > WEBP_FILE_LIMIT) {
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		return false;
	}

	uint8_t *data = malloc((size_t)size);
	if (data == NULL) {
		return false;
	}
	if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
		free(data);
		return false;
	}

	int width = 0;
	int height = 0;
	if (!WebPGetInfo(data, (size_t)size, &width, &height) ||
		!sweetwall_image_dimensions_ok(
			(uint32_t)width, (uint32_t)height)) {
		free(data);
		return false;
	}

	size_t stride = (size_t)width * sizeof(uint32_t);
	size_t out_size = stride * (size_t)height;
	img->pixels = malloc(out_size);
	if (img->pixels == NULL) {
		free(data);
		return false;
	}
	img->width = (uint32_t)width;
	img->height = (uint32_t)height;

	if (WebPDecodeBGRAInto(data, (size_t)size, (uint8_t *)img->pixels,
		    out_size, (int)stride) == NULL) {
		free(data);
		free(img->pixels);
		img->pixels = NULL;
		return false;
	}

	free(data);
	force_opaque(img->pixels, (size_t)width * height);
	return true;
}

// --- dispatch ---

static bool is_png(const uint8_t *sig, size_t n) {
	static const uint8_t magic[8] = {
		0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	return n >= 8 && memcmp(sig, magic, 8) == 0;
}

static bool is_jpeg(const uint8_t *sig, size_t n) {
	return n >= 3 && sig[0] == 0xff && sig[1] == 0xd8 && sig[2] == 0xff;
}

static bool is_webp(const uint8_t *sig, size_t n) {
	return n >= 12 && memcmp(sig, "RIFF", 4) == 0 &&
	       memcmp(sig + 8, "WEBP", 4) == 0;
}

bool sweetwall_image_decode(struct sweetwall_image *img, const char *path) {
	*img = (struct sweetwall_image){0};

	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		return false;
	}

	uint8_t sig[12];
	size_t got = fread(sig, 1, sizeof(sig), fp);
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}

	bool ok;
	if (is_png(sig, got)) {
		ok = decode_png(fp, img);
	} else if (is_jpeg(sig, got)) {
		ok = decode_jpeg(fp, img);
	} else if (is_webp(sig, got)) {
		ok = decode_webp(fp, img);
	} else {
		ok = false;
	}

	fclose(fp);
	return ok;
}

void sweetwall_image_free(struct sweetwall_image *img) {
	free(img->pixels);
	img->pixels = NULL;
	img->width = 0;
	img->height = 0;
}
