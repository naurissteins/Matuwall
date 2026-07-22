#ifndef SWEETWALL_RENDER_COLOR_H
#define SWEETWALL_RENDER_COLOR_H

#include <stdint.h>

struct sweetwall_color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

static inline uint32_t sweetwall_color_argb(struct sweetwall_color color) {
	uint32_t a = color.a;
	uint32_t r = ((uint32_t)color.r * a + 127) / 255;
	uint32_t g = ((uint32_t)color.g * a + 127) / 255;
	uint32_t b = ((uint32_t)color.b * a + 127) / 255;
	return a << 24 | r << 16 | g << 8 | b;
}

#endif
