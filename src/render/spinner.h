#ifndef MATUWALL_RENDER_SPINNER_H
#define MATUWALL_RENDER_SPINNER_H

#include <stdint.h>

#include "render/color.h"
#include "render/draw.h"

#define MATUWALL_SPINNER_INTERVAL_MS 125

uint8_t matuwall_spinner_alpha(int64_t now_ms);

void matuwall_spinner_draw(struct matuwall_buffer *buffer,
	const struct matuwall_clip *clip, int32_t cx, int32_t cy,
	int32_t radius, struct matuwall_color base, uint8_t alpha);

#endif
