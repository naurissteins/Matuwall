#ifndef SWEETWALL_RENDER_SPINNER_H
#define SWEETWALL_RENDER_SPINNER_H

#include <stdint.h>

#include "render/color.h"
#include "render/draw.h"

#define SWEETWALL_SPINNER_INTERVAL_MS 125

uint8_t sweetwall_spinner_alpha(int64_t now_ms);

void sweetwall_spinner_draw(struct sweetwall_buffer *buffer,
	const struct sweetwall_clip *clip, int32_t cx, int32_t cy,
	int32_t radius, struct sweetwall_color base, uint8_t alpha);

#endif
