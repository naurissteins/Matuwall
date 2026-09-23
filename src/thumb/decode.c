#include "thumb/decode.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

#include "thumb/scale.h"

#define MAX_DIMENSION 16384u
#define MAX_PIXELS (1u << 26)
#define MAX_THUMBNAIL_PIXELS (1u << 24)
#define WEBP_FILE_LIMIT (64UL * 1024 * 1024)

static bool stop_requested(const atomic_bool *stop) {
	return stop != NULL && atomic_load_explicit(stop, memory_order_relaxed);
}

bool matuwall_image_dimensions_ok(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0 || width > MAX_DIMENSION ||
		height > MAX_DIMENSION) {
		return false;
	}
	return (uint64_t)width * height <= MAX_PIXELS;
}

static bool decode_dimensions_ok(
	uint32_t width, uint32_t height, enum matuwall_decode_purpose purpose) {
	if (!matuwall_image_dimensions_ok(width, height)) {
		return false;
	}
	uint64_t limit = purpose == MATUWALL_DECODE_THUMBNAIL
				 ? MAX_THUMBNAIL_PIXELS
				 : MAX_PIXELS;
	return (uint64_t)width * height <= limit;
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

static void jpeg_scale_for_target(struct jpeg_decompress_struct *cinfo,
	uint32_t target_w, uint32_t target_h) {
	static const unsigned int denominators[] = {8, 4, 2};
	if (target_w == 0 || target_h == 0) {
		return;
	}

	// Keep enough decoded pixels for the final cover crop
	unsigned int denominator = 1;
	for (size_t i = 0; i < sizeof(denominators) / sizeof(denominators[0]);
		i++) {
		cinfo->scale_num = 1;
		cinfo->scale_denom = denominators[i];
		jpeg_calc_output_dimensions(cinfo);
		if (cinfo->output_width >= target_w &&
			cinfo->output_height >= target_h) {
			denominator = denominators[i];
			break;
		}
	}
	cinfo->scale_num = 1;
	cinfo->scale_denom = denominator;
	jpeg_calc_output_dimensions(cinfo);
	if (cinfo->output_width == target_w &&
		cinfo->output_height == target_h) {
		return;
	}

	// M/8 lacks SIMD, so it only wins when it also removes the scale pass
	for (unsigned int num = 7; num > 0; num--) {
		cinfo->scale_num = num;
		cinfo->scale_denom = 8;
		jpeg_calc_output_dimensions(cinfo);
		if (cinfo->output_width == target_w &&
			cinfo->output_height == target_h) {
			return;
		}
	}
	cinfo->scale_num = 1;
	cinfo->scale_denom = denominator;
}

static bool decode_jpeg(FILE *fp, struct matuwall_image *img, uint32_t target_w,
	uint32_t target_h, enum matuwall_decode_purpose purpose,
	const atomic_bool *stop) {
	struct jpeg_decompress_struct cinfo = {0};
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

	if (!matuwall_image_dimensions_ok(
		    cinfo.image_width, cinfo.image_height)) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	jpeg_scale_for_target(&cinfo, target_w, target_h);
	cinfo.out_color_space = JCS_EXT_BGRA;
	jpeg_start_decompress(&cinfo);

	uint32_t width = cinfo.output_width;
	uint32_t height = cinfo.output_height;
	if (!decode_dimensions_ok(width, height, purpose)) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}
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
		if (stop_requested(stop) ||
			jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
			jpeg_destroy_decompress(&cinfo);
			matuwall_image_free(img);
			return false;
		}
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return true;
}

// --- png ---

struct png_decode_buffers {
	png_bytep *rows;
	png_bytep row;
	uint64_t *sums;
	struct matuwall_span *columns;
};

static void png_buffers_free(struct png_decode_buffers *buffers) {
	free((void *)buffers->rows);
	free(buffers->row);
	free(buffers->sums);
	free(buffers->columns);
	*buffers = (struct png_decode_buffers){0};
}

// rows are packed RGB, sums stay in B, G, R planes for write_png_row
static void accumulate_png_row(const png_byte *row, uint64_t *sums,
	const struct matuwall_span *columns, uint32_t target_w) {
	for (uint32_t ox = 0; ox < target_w; ox++) {
		const png_byte *p = row + (size_t)columns[ox].start * 3;
		const png_byte *end = p + (size_t)columns[ox].count * 3;
		// locals, since byte loads may alias sums, span fits 32 bits
		uint32_t r = 0;
		uint32_t g = 0;
		uint32_t b = 0;
		for (; p < end; p += 3) {
			r += p[0];
			g += p[1];
			b += p[2];
		}
		sums[ox] += b;
		sums[target_w + ox] += g;
		sums[target_w * 2 + ox] += r;
	}
}

static void write_png_row(uint32_t *dst, const uint64_t *sums,
	const struct matuwall_span *columns, uint32_t target_w,
	uint32_t source_rows) {
	if (source_rows == 0) {
		return;
	}
	for (uint32_t ox = 0; ox < target_w; ox++) {
		uint64_t count = (uint64_t)columns[ox].count * source_rows;
		if (count == 0) {
			dst[ox] = 0xff000000u;
			continue;
		}
		// A 1x1 box is the source pixel, skip the divisions
		if (count == 1) {
			dst[ox] = 0xff000000u |
				  (uint32_t)sums[target_w * 2 + ox] << 16 |
				  (uint32_t)sums[target_w + ox] << 8 |
				  (uint32_t)sums[ox];
			continue;
		}
		dst[ox] = 0xff000000u |
			  (uint32_t)(sums[target_w * 2 + ox] / count) << 16 |
			  (uint32_t)(sums[target_w + ox] / count) << 8 |
			  (uint32_t)(sums[ox] / count);
	}
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
	// w0hole image decode lands in opaque ARGB8888, streaming keeps RGB
	if (argb) {
		png_set_bgr(png);
		png_set_filler(png, 0xff, PNG_FILLER_AFTER);
	}
}

static bool decode_interlaced_png(png_structp png, struct matuwall_image *img,
	uint32_t width, uint32_t height, enum matuwall_decode_purpose purpose,
	int passes, struct png_decode_buffers *buffers,
	const atomic_bool *stop) {
	if (!decode_dimensions_ok(width, height, purpose)) {
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
			if (stop_requested(stop)) {
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
	uint32_t width, uint32_t height, uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose,
	struct png_decode_buffers *buffers, const atomic_bool *stop) {
	if (!decode_dimensions_ok(target_w, target_h, purpose)) {
		return false;
	}
	uint32_t crop_x;
	uint32_t crop_y;
	uint32_t crop_w;
	uint32_t crop_h;
	matuwall_cover_crop(width, height, target_w, target_h, &crop_x, &crop_y,
		&crop_w, &crop_h);

	img->width = target_w;
	img->height = target_h;
	img->pixels = malloc((size_t)target_w * target_h * sizeof(uint32_t));
	buffers->row = malloc((size_t)width * 3);
	buffers->sums = calloc((size_t)target_w * 3, sizeof(uint64_t));
	buffers->columns = malloc((size_t)target_w * sizeof(*buffers->columns));
	if (img->pixels == NULL || buffers->row == NULL ||
		buffers->sums == NULL || buffers->columns == NULL) {
		png_longjmp(png, 1);
	}
	// column spans never vary by row, so they are derived once
	for (uint32_t ox = 0; ox < target_w; ox++) {
		buffers->columns[ox] =
			matuwall_axis_span(crop_x, crop_w, target_w, ox);
	}

	uint32_t next_y = 0;
	uint32_t loaded_y = UINT32_MAX;
	for (uint32_t oy = 0; oy < target_h; oy++) {
		struct matuwall_span rows =
			matuwall_axis_span(crop_y, crop_h, target_h, oy);
		uint32_t sy0 = rows.start;
		uint32_t sy1 = sy0 + rows.count;
		memset(buffers->sums, 0,
			(size_t)target_w * 3 * sizeof(uint64_t));
		for (uint32_t sy = sy0; sy < sy1; sy++) {
			while (next_y <= sy) {
				if (stop_requested(stop)) {
					png_longjmp(png, 1);
				}
				png_read_row(png, buffers->row, NULL);
				loaded_y = next_y++;
			}
			if (loaded_y != sy) {
				png_longjmp(png, 1);
			}
			accumulate_png_row(buffers->row, buffers->sums,
				buffers->columns, target_w);
		}
		write_png_row(img->pixels + (size_t)oy * target_w,
			buffers->sums, buffers->columns, target_w, rows.count);
	}

	while (next_y < height) {
		if (stop_requested(stop)) {
			png_longjmp(png, 1);
		}
		png_read_row(png, buffers->row, NULL);
		next_y++;
	}
	png_read_end(png, NULL);
	return true;
}

static bool decode_png(FILE *fp, struct matuwall_image *img, uint32_t target_w,
	uint32_t target_h, enum matuwall_decode_purpose purpose,
	const atomic_bool *stop) {
	png_structp png =
		png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
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
		free(img->pixels);
		img->pixels = NULL;
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

	bool ok = interlaced
			  ? decode_interlaced_png(png, img, width, height,
				    purpose, passes, buffers, stop)
			  : decode_png_rows(png, img, width, height, target_w,
				    target_h, purpose, buffers, stop);
	png_buffers_free(buffers);
	free(buffers);
	png_destroy_read_struct(&png, &info, NULL);
	return ok;
}

// --- webp ---

static bool read_webp(FILE *fp, uint8_t **data, size_t *data_size) {
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

	*data = malloc((size_t)size);
	if (*data == NULL) {
		return false;
	}
	if (fread(*data, 1, (size_t)size, fp) != (size_t)size) {
		free(*data);
		*data = NULL;
		return false;
	}
	*data_size = (size_t)size;
	return true;
}

static bool configure_webp(WebPDecoderConfig *config, const uint8_t *data,
	size_t data_size, uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose) {
	if (!WebPInitDecoderConfig(config) ||
		WebPGetFeatures(data, data_size, &config->input) !=
			VP8_STATUS_OK ||
		!matuwall_image_dimensions_ok((uint32_t)config->input.width,
			(uint32_t)config->input.height) ||
		!decode_dimensions_ok(target_w, target_h, purpose)) {
		return false;
	}

	uint32_t crop_x;
	uint32_t crop_y;
	uint32_t crop_w;
	uint32_t crop_h;
	matuwall_cover_crop((uint32_t)config->input.width,
		(uint32_t)config->input.height, target_w, target_h, &crop_x,
		&crop_y, &crop_w, &crop_h);
	config->options.use_cropping = 1;
	config->options.crop_left = (int)crop_x;
	config->options.crop_top = (int)crop_y;
	config->options.crop_width = (int)crop_w;
	config->options.crop_height = (int)crop_h;
	config->options.use_scaling = 1;
	config->options.scaled_width = (int)target_w;
	config->options.scaled_height = (int)target_h;
	return true;
}

static bool decode_webp(FILE *fp, struct matuwall_image *img, uint32_t target_w,
	uint32_t target_h, enum matuwall_decode_purpose purpose,
	const atomic_bool *stop) {
	uint8_t *data = NULL;
	size_t data_size;
	WebPDecoderConfig config;
	if (!read_webp(fp, &data, &data_size) ||
		!configure_webp(&config, data, data_size, target_w, target_h,
			purpose)) {
		free(data);
		return false;
	}

	size_t stride = (size_t)target_w * sizeof(uint32_t);
	size_t out_size = stride * target_h;
	img->pixels = malloc(out_size);
	if (img->pixels == NULL) {
		free(data);
		return false;
	}
	img->width = target_w;
	img->height = target_h;
	config.output.colorspace = MODE_BGRA;
	config.output.is_external_memory = 1;
	config.output.u.RGBA.rgba = (uint8_t *)img->pixels;
	config.output.u.RGBA.stride = (int)stride;
	config.output.u.RGBA.size = out_size;

	if (stop_requested(stop)) {
		WebPFreeDecBuffer(&config.output);
		free(data);
		matuwall_image_free(img);
		return false;
	}
	VP8StatusCode status = WebPDecode(data, data_size, &config);
	bool stopped = stop_requested(stop);
	WebPFreeDecBuffer(&config.output);
	if (status != VP8_STATUS_OK || stopped) {
		free(data);
		matuwall_image_free(img);
		return false;
	}

	free(data);
	force_opaque(img->pixels, (size_t)target_w * target_h);
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

bool matuwall_image_decode(struct matuwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose, const atomic_bool *stop) {
	*img = (struct matuwall_image){0};
	if (!decode_dimensions_ok(target_w, target_h, purpose) ||
		stop_requested(stop)) {
		return false;
	}

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
		ok = decode_png(fp, img, target_w, target_h, purpose, stop);
	} else if (is_jpeg(sig, got)) {
		ok = decode_jpeg(fp, img, target_w, target_h, purpose, stop);
	} else if (is_webp(sig, got)) {
		ok = decode_webp(fp, img, target_w, target_h, purpose, stop);
	} else {
		ok = false;
	}

	fclose(fp);
	return ok;
}

void matuwall_image_free(struct matuwall_image *img) {
	free(img->pixels);
	img->pixels = NULL;
	img->width = 0;
	img->height = 0;
}
