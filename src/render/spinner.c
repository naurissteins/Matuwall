#include "render/spinner.h"

#define PULSE_PERIOD_MS 1400
#define ALPHA_MIN 60
#define ALPHA_MAX 220

uint8_t matuwall_spinner_alpha(int64_t now_ms) {
	int64_t phase = now_ms % PULSE_PERIOD_MS;
	int64_t half = PULSE_PERIOD_MS / 2;

	int64_t up = phase < half ? phase : PULSE_PERIOD_MS - phase;
	int64_t span = ALPHA_MAX - ALPHA_MIN;
	return (uint8_t)(ALPHA_MIN + up * span / half);
}

void matuwall_spinner_draw(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t cx, int32_t cy,
	int32_t radius, struct matuwall_color base, uint8_t alpha) {
	if (radius <= 0) {
		return;
	}

	base.a = alpha;
	uint32_t color = matuwall_color_argb(base);
	matuwall_draw_rounded_rect(buffer, clip, cx - radius, cy - radius,
		radius * 2, radius * 2, radius, color);
}
