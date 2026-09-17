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

struct matuwall_bilinear_row {
	const struct matuwall_bilinear_sampler *sampler;
	const uint32_t *top;
	const uint32_t *bottom;
	int64_t position;
	uint32_t y_fraction;
};

struct matuwall_bilinear_axis {
	uint32_t first;
	uint32_t fraction;
};

bool matuwall_bilinear_sampler_init(struct matuwall_bilinear_sampler *sampler,
	const uint32_t *pixels, uint32_t width, uint32_t height,
	uint32_t target_width, uint32_t target_height);

uint32_t matuwall_bilinear_sample(
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	uint32_t y);

void matuwall_bilinear_row_init(struct matuwall_bilinear_row *row,
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	uint32_t y);

uint32_t matuwall_bilinear_row_next(struct matuwall_bilinear_row *row);

void matuwall_bilinear_axes_init(
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	struct matuwall_bilinear_axis *axes, uint32_t count);

void matuwall_bilinear_row_span(struct matuwall_bilinear_row *row,
	const struct matuwall_bilinear_axis *axes, uint32_t *dst,
	uint32_t count);

#endif
