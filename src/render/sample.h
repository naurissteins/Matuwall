#ifndef SWEETWALL_RENDER_SAMPLE_H
#define SWEETWALL_RENDER_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

struct sweetwall_bilinear_sampler {
	const uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	int64_t step_x;
	int64_t step_y;
	int64_t start_x;
	int64_t start_y;
};

bool sweetwall_bilinear_sampler_init(struct sweetwall_bilinear_sampler *sampler,
	const uint32_t *pixels, uint32_t width, uint32_t height,
	uint32_t target_width, uint32_t target_height);

uint32_t sweetwall_bilinear_sample(
	const struct sweetwall_bilinear_sampler *sampler, uint32_t x,
	uint32_t y);

#endif
