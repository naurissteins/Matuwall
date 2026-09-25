#include "render/sample.h"

#include <stddef.h>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

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

// Two channels per multiply: an 8-bit weight keeps each 16-bit lane below
// 65536, so no carry crosses into the neighbouring channel
static uint32_t lerp_pixel(uint32_t from, uint32_t to, uint32_t fraction) {
	uint32_t weight = (fraction + 0x80) >> 8;
	uint32_t keep = 256 - weight;
	uint32_t rb = ((from & 0x00ff00ff) * keep + (to & 0x00ff00ff) * weight +
			      0x00800080) >>
		      8;
	uint32_t ag = ((from >> 8) & 0x00ff00ff) * keep +
		      ((to >> 8) & 0x00ff00ff) * weight + 0x00800080;
	return (rb & 0x00ff00ff) | (ag & 0xff00ff00);
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

#ifdef __SSE2__
// lerp_pixel on 16-bit lanes: same weights and rounding, so the output is
// bit-identical and no lane can exceed 255 * 256 + 0x80
static void row_span_sse2(const struct matuwall_bilinear_row *row,
	const struct matuwall_bilinear_axis *axes, uint32_t *dst,
	uint32_t count) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i round = _mm_set1_epi16(0x80);
	int16_t y_weight = (int16_t)((row->y_fraction + 0x80) >> 8);
	int16_t y_keep = (int16_t)(256 - y_weight);
	// low half weighs the top row, high half the bottom row
	const __m128i vertical = _mm_set_epi16(y_weight, y_weight, y_weight,
		y_weight, y_keep, y_keep, y_keep, y_keep);
	uint32_t width = row->sampler->width;

	for (uint32_t i = 0; i < count; i++) {
		uint32_t first = axes[i].first;
		uint32_t second = first + (first + 1 < width);
		int16_t weight = (int16_t)((axes[i].fraction + 0x80) >> 8);
		__m128i from = _mm_unpacklo_epi32(
			_mm_cvtsi32_si128((int32_t)row->top[first]),
			_mm_cvtsi32_si128((int32_t)row->bottom[first]));
		__m128i to = _mm_unpacklo_epi32(
			_mm_cvtsi32_si128((int32_t)row->top[second]),
			_mm_cvtsi32_si128((int32_t)row->bottom[second]));
		from = _mm_unpacklo_epi8(from, zero);
		to = _mm_unpacklo_epi8(to, zero);

		// top and bottom horizontal lerps side by side
		__m128i across = _mm_add_epi16(
			_mm_mullo_epi16(
				from, _mm_set1_epi16((int16_t)(256 - weight))),
			_mm_mullo_epi16(to, _mm_set1_epi16(weight)));
		across = _mm_srli_epi16(_mm_add_epi16(across, round), 8);

		__m128i down = _mm_mullo_epi16(across, vertical);
		down = _mm_add_epi16(down, _mm_srli_si128(down, 8));
		down = _mm_srli_epi16(_mm_add_epi16(down, round), 8);
		dst[i] = (uint32_t)_mm_cvtsi128_si32(
			_mm_packus_epi16(down, down));
	}
}
#endif

void matuwall_bilinear_row_span(struct matuwall_bilinear_row *row,
	const struct matuwall_bilinear_axis *axes, uint32_t *dst,
	uint32_t count) {
#ifdef __SSE2__
	row_span_sse2(row, axes, dst, count);
#else
	for (uint32_t i = 0; i < count; i++) {
		uint32_t first = axes[i].first;
		uint32_t second = first + (first + 1 < row->sampler->width);
		uint32_t upper = lerp_pixel(
			row->top[first], row->top[second], axes[i].fraction);
		uint32_t lower = lerp_pixel(row->bottom[first],
			row->bottom[second], axes[i].fraction);
		dst[i] = lerp_pixel(upper, lower, row->y_fraction);
	}
#endif
	row->position += (int64_t)count * row->sampler->step_x;
}
