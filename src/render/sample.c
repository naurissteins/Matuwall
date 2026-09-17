#include "render/sample.h"

#include <stddef.h>

#define SAMPLE_SHIFT 16
#define SAMPLE_ONE (UINT32_C(1) << SAMPLE_SHIFT)
#define SAMPLE_HALF (SAMPLE_ONE / 2)

struct sample_axis {
	uint32_t first;
	uint32_t second;
	uint32_t fraction;
};

static struct sample_axis sample_axis(int64_t position, uint32_t size) {
	if (position <= 0 || size == 1) {
		return (struct sample_axis){0};
	}
	int64_t last = (int64_t)(size - 1) << SAMPLE_SHIFT;
	if (position >= last) {
		return (struct sample_axis){
			.first = size - 1,
			.second = size - 1,
		};
	}
	uint32_t first = (uint32_t)(position >> SAMPLE_SHIFT);
	return (struct sample_axis){
		.first = first,
		.second = first + 1,
		.fraction = (uint32_t)position & (SAMPLE_ONE - 1),
	};
}

static uint32_t lerp_channel(uint32_t from, uint32_t to, uint32_t fraction) {
	return (from * (SAMPLE_ONE - fraction) + to * fraction + SAMPLE_HALF) >>
	       SAMPLE_SHIFT;
}

static uint32_t lerp_pixel(uint32_t from, uint32_t to, uint32_t fraction) {
	uint32_t a = lerp_channel(from >> 24, to >> 24, fraction);
	uint32_t r =
		lerp_channel((from >> 16) & 0xff, (to >> 16) & 0xff, fraction);
	uint32_t g =
		lerp_channel((from >> 8) & 0xff, (to >> 8) & 0xff, fraction);
	uint32_t b = lerp_channel(from & 0xff, to & 0xff, fraction);
	return a << 24 | r << 16 | g << 8 | b;
}

bool matuwall_bilinear_sampler_init(struct matuwall_bilinear_sampler *sampler,
	const uint32_t *pixels, uint32_t width, uint32_t height,
	uint32_t target_width, uint32_t target_height) {
	if (sampler == NULL || pixels == NULL || width == 0 || height == 0 ||
		target_width == 0 || target_height == 0) {
		return false;
	}
	int64_t step_x = ((int64_t)width << SAMPLE_SHIFT) / target_width;
	int64_t step_y = ((int64_t)height << SAMPLE_SHIFT) / target_height;
	*sampler = (struct matuwall_bilinear_sampler){
		.pixels = pixels,
		.width = width,
		.height = height,
		.step_x = step_x,
		.step_y = step_y,
		.start_x = step_x / 2 - SAMPLE_HALF,
		.start_y = step_y / 2 - SAMPLE_HALF,
	};
	return true;
}

uint32_t matuwall_bilinear_sample(
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	uint32_t y) {
	struct matuwall_bilinear_row row;
	matuwall_bilinear_row_init(&row, sampler, x, y);
	return matuwall_bilinear_row_next(&row);
}

void matuwall_bilinear_row_init(struct matuwall_bilinear_row *row,
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	uint32_t y) {
	struct sample_axis sy =
		sample_axis(sampler->start_y + (int64_t)y * sampler->step_y,
			sampler->height);
	*row = (struct matuwall_bilinear_row){
		.sampler = sampler,
		.top = sampler->pixels + (size_t)sy.first * sampler->width,
		.bottom = sampler->pixels + (size_t)sy.second * sampler->width,
		.position = sampler->start_x + (int64_t)x * sampler->step_x,
		.y_fraction = sy.fraction,
	};
}

uint32_t matuwall_bilinear_row_next(struct matuwall_bilinear_row *row) {
	struct sample_axis sx = sample_axis(row->position, row->sampler->width);
	row->position += row->sampler->step_x;
	uint32_t upper = lerp_pixel(
		row->top[sx.first], row->top[sx.second], sx.fraction);
	uint32_t lower = lerp_pixel(
		row->bottom[sx.first], row->bottom[sx.second], sx.fraction);
	return lerp_pixel(upper, lower, row->y_fraction);
}

void matuwall_bilinear_axes_init(
	const struct matuwall_bilinear_sampler *sampler, uint32_t x,
	struct matuwall_bilinear_axis *axes, uint32_t count) {
	int64_t position = sampler->start_x + (int64_t)x * sampler->step_x;
	for (uint32_t i = 0; i < count; i++) {
		struct sample_axis axis = sample_axis(position, sampler->width);
		axes[i] = (struct matuwall_bilinear_axis){
			.first = axis.first,
			.fraction = axis.fraction,
		};
		position += sampler->step_x;
	}
}

void matuwall_bilinear_row_span(struct matuwall_bilinear_row *row,
	const struct matuwall_bilinear_axis *axes, uint32_t *dst,
	uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		uint32_t first = axes[i].first;
		uint32_t second = first + (first + 1 < row->sampler->width);
		uint32_t upper = lerp_pixel(
			row->top[first], row->top[second], axes[i].fraction);
		uint32_t lower = lerp_pixel(row->bottom[first],
			row->bottom[second], axes[i].fraction);
		dst[i] = lerp_pixel(upper, lower, row->y_fraction);
	}
	row->position += (int64_t)count * row->sampler->step_x;
}
