#include "thumb/decode.h"

#include <fcntl.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>

#include "thumb/scale.h"

#define MAX_DIMENSION 16384u
#define MAX_THUMBNAIL_PIXELS (1u << 24)
#define WEBP_FILE_LIMIT (64UL * 1024 * 1024)
// refuse decodes whose transient memory estimate passes this
#define DECODE_MEMORY_LIMIT ((uint64_t)512 * 1024 * 1024)

// one decode's target and limits, shared by every format
struct decode_job {
	uint32_t target_w;
	uint32_t target_h;
	enum matuwall_decode_purpose purpose;
	const atomic_bool *stop;
	const struct matuwall_decode_budget *budget;
};

static bool stop_requested(const atomic_bool *stop) {
	return stop != NULL && atomic_load_explicit(stop, memory_order_relaxed);
}

// called once per decode, before its large allocations
static bool reserve(const struct decode_job *job, uint64_t bytes) {
	if (bytes > DECODE_MEMORY_LIMIT) {
		return false;
	}
	return job->budget == NULL ||
	       job->budget->reserve(job->budget->user_data, bytes);
}

static uint64_t target_bytes(const struct decode_job *job) {
	return (uint64_t)job->target_w * job->target_h * sizeof(uint32_t);
}

bool matuwall_image_dimensions_ok(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0 || width > MAX_DIMENSION ||
		height > MAX_DIMENSION) {
		return false;
	}
	return (uint64_t)width * height <= MATUWALL_IMAGE_MAX_PIXELS;
}

static bool decode_dimensions_ok(
	uint32_t width, uint32_t height, enum matuwall_decode_purpose purpose) {
	if (!matuwall_image_dimensions_ok(width, height)) {
		return false;
	}
	uint64_t limit = purpose == MATUWALL_DECODE_THUMBNAIL
				 ? MAX_THUMBNAIL_PIXELS
				 : MATUWALL_IMAGE_MAX_PIXELS;
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

static void jpeg_on_message(j_common_ptr cinfo) {
	(void)cinfo;
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

// multi-scan files keep every source coefficient, whatever the DCT scale
static uint64_t jpeg_coefficient_bytes(struct jpeg_decompress_struct *cinfo) {
	if (!jpeg_has_multiple_scans(cinfo)) {
		return 0;
	}
	uint64_t bytes = 0;
	for (int ci = 0; ci < cinfo->num_components; ci++) {
		const jpeg_component_info *comp = &cinfo->comp_info[ci];
		uint64_t h = (uint64_t)comp->h_samp_factor;
		uint64_t v = (uint64_t)comp->v_samp_factor;
		uint64_t columns = (comp->width_in_blocks + h - 1) / h * h;
		uint64_t rows = (comp->height_in_blocks + v - 1) / v * v;
		bytes += columns * rows * DCTSIZE2 * sizeof(JCOEF);
	}
	return bytes;
}

// heap state, so the error longjmp can still free it
struct jpeg_stream {
	uint8_t *row;
	struct matuwall_row_scaler scaler;
};

static void jpeg_stream_free(struct jpeg_stream *stream) {
	free(stream->row);
	matuwall_row_scaler_finish(&stream->scaler);
	free(stream);
}

static bool read_jpeg_direct(struct jpeg_decompress_struct *cinfo,
	struct matuwall_image *img, const atomic_bool *stop) {
	while (cinfo->output_scanline < img->height) {
		JSAMPROW row =
			(JSAMPROW)(img->pixels +
				   (size_t)cinfo->output_scanline * img->width);
		if (stop_requested(stop) ||
			jpeg_read_scanlines(cinfo, &row, 1) != 1) {
			return false;
		}
	}
	jpeg_finish_decompress(cinfo);
	return true;
}

// rows below the cover crop are never decoded
static bool read_jpeg_streamed(struct jpeg_decompress_struct *cinfo,
	struct matuwall_image *img, struct jpeg_stream *stream,
	const atomic_bool *stop) {
	stream->row = malloc((size_t)cinfo->output_width * 3);
	if (stream->row == NULL ||
		!matuwall_row_scaler_init(&stream->scaler, cinfo->output_width,
			cinfo->output_height, img->width, img->height,
			img->pixels)) {
		return false;
	}
	while (!matuwall_row_scaler_done(&stream->scaler)) {
		uint32_t y = cinfo->output_scanline;
		JSAMPROW row = stream->row;
		if (stop_requested(stop) || y >= cinfo->output_height ||
			jpeg_read_scanlines(cinfo, &row, 1) != 1) {
			return false;
		}
		matuwall_row_scaler_push(&stream->scaler, y, stream->row);
	}
	return true;
}

static bool decode_jpeg(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job) {
	struct jpeg_stream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		return false;
	}
	struct jpeg_decompress_struct cinfo = {0};
	struct jpeg_guard guard;
	cinfo.err = jpeg_std_error(&guard.base);
	guard.base.error_exit = jpeg_on_error;
	guard.base.output_message = jpeg_on_message;

	if (setjmp(guard.jmp)) {
		jpeg_destroy_decompress(&cinfo);
		jpeg_stream_free(stream);
		matuwall_image_free(img);
		return false;
	}

	jpeg_create_decompress(&cinfo);
	// backstop for the estimate below: libjpeg fails instead of growing
	cinfo.mem->max_memory_to_use = (long)DECODE_MEMORY_LIMIT;
	jpeg_stdio_src(&cinfo, fp);
	jpeg_read_header(&cinfo, TRUE);

	bool ok = matuwall_image_dimensions_ok(
		cinfo.image_width, cinfo.image_height);
	if (ok) {
		jpeg_scale_for_target(&cinfo, job->target_w, job->target_h);
	}
	bool direct = cinfo.output_width == job->target_w &&
		      cinfo.output_height == job->target_h;
	uint64_t peak = jpeg_coefficient_bytes(&cinfo) + target_bytes(job);
	if (!direct) {
		peak += (uint64_t)cinfo.output_width * 3 +
			matuwall_row_scaler_bytes(job->target_w);
	}
	ok = ok && reserve(job, peak);
	if (ok) {
		cinfo.out_color_space = direct ? JCS_EXT_BGRA : JCS_RGB;
		jpeg_start_decompress(&cinfo);
		img->width = job->target_w;
		img->height = job->target_h;
		img->pixels = malloc(target_bytes(job));
		ok = img->pixels != NULL &&
		     (direct ? read_jpeg_direct(&cinfo, img, job->stop)
			     : read_jpeg_streamed(
				       &cinfo, img, stream, job->stop));
	}

	jpeg_destroy_decompress(&cinfo);
	jpeg_stream_free(stream);
	if (!ok) {
		matuwall_image_free(img);
	}
	return ok;
}

// --- png ---

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
	// w0hole image decode lands in opaque ARGB8888, streaming keeps RGB
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
	img->pixels = malloc(target_bytes(job));
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

static bool decode_png(
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

// lossless decodes the whole ARGB image, lossy alpha keeps a full plane
static uint64_t webp_working_bytes(const WebPBitstreamFeatures *features) {
	uint64_t pixels =
		(uint64_t)features->width * (uint64_t)features->height;
	if (features->format == 2) {
		return pixels * sizeof(uint32_t);
	}
	return features->has_alpha ? pixels : 0;
}

static bool configure_webp(WebPDecoderConfig *config, const uint8_t *data,
	size_t data_size, const struct decode_job *job) {
	if (!WebPInitDecoderConfig(config) ||
		WebPGetFeatures(data, data_size, &config->input) !=
			VP8_STATUS_OK ||
		!matuwall_image_dimensions_ok((uint32_t)config->input.width,
			(uint32_t)config->input.height)) {
		return false;
	}
	uint32_t target_w = job->target_w;
	uint32_t target_h = job->target_h;

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

// compressed file is read before its format is known and is counted
static bool decode_webp(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job) {
	uint32_t target_w = job->target_w;
	uint32_t target_h = job->target_h;
	const atomic_bool *stop = job->stop;
	uint8_t *data = NULL;
	size_t data_size;
	WebPDecoderConfig config;
	if (!read_webp(fp, &data, &data_size) ||
		!configure_webp(&config, data, data_size, job) ||
		!reserve(job, data_size + target_bytes(job) +
				      webp_working_bytes(&config.input))) {
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

// O_NONBLOCK keeps a FIFO named like an image from wedging a worker
static FILE *open_regular(const char *path) {
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		return NULL;
	}
	struct stat info;
	if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
		close(fd);
		return NULL;
	}
	FILE *fp = fdopen(fd, "rb");
	if (fp == NULL) {
		close(fd);
	}
	return fp;
}

bool matuwall_image_decode(struct matuwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose, const atomic_bool *stop,
	const struct matuwall_decode_budget *budget) {
	*img = (struct matuwall_image){0};
	if (!decode_dimensions_ok(target_w, target_h, purpose) ||
		stop_requested(stop)) {
		return false;
	}
	const struct decode_job job = {
		.target_w = target_w,
		.target_h = target_h,
		.purpose = purpose,
		.stop = stop,
		.budget = budget,
	};

	FILE *fp = open_regular(path);
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
		ok = decode_png(fp, img, &job);
	} else if (is_jpeg(sig, got)) {
		ok = decode_jpeg(fp, img, &job);
	} else if (is_webp(sig, got)) {
		ok = decode_webp(fp, img, &job);
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
