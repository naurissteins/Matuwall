#include "thumb/decode/format.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <webp/decode.h>

#include "thumb/scale.h"

#define WEBP_FILE_LIMIT (64UL * 1024 * 1024)

static void force_opaque(uint32_t *pixels, size_t count) {
	for (size_t i = 0; i < count; i++) {
		pixels[i] |= 0xff000000u;
	}
}

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
bool matuwall_decode_webp(
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
	img->pixels = alloc_target(job);
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
