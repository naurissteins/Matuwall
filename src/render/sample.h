#ifndef MATUWALL_RENDER_SAMPLE_H
#define MATUWALL_RENDER_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

struct matuwall_bilinear_sampler {
	const uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	int64_t step_x;
	int64_t step_y;
	int64_t start_x;
	int64_t start_y;
};

bool matuwall_bilinear_sampler_init(struct matuwall_bilinear_sampler *sampler,
	const uint32_t *pixels, uint32_t width, uint32_t height,
	uint32_t target_width, uint32_t target_height);

uint32_t matuwall_bilinear_sample(
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	uint32_t y);

#endif
