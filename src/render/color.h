#ifndef MATUWALL_RENDER_COLOR_H
#define MATUWALL_RENDER_COLOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MATUWALL_OPAQUE 0xff

struct matuwall_color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

static inline uint32_t matuwall_color_argb(struct matuwall_color color) {
	uint32_t a = color.a;
	uint32_t r = ((uint32_t)color.r * a + 127) / 255;
	uint32_t g = ((uint32_t)color.g * a + 127) / 255;
	uint32_t b = ((uint32_t)color.b * a + 127) / 255;
	return a << 24 | r << 16 | g << 8 | b;
}

static inline uint8_t matuwall_alpha_mul(uint8_t a, uint8_t b) {
	return (uint8_t)(((uint32_t)a * b + 127) / 255);
}

static inline uint32_t matuwall_color_fade(uint32_t argb, uint8_t opacity) {
	if (opacity == MATUWALL_OPAQUE) {
		return argb;
	}
	uint32_t a = matuwall_alpha_mul((uint8_t)(argb >> 24), opacity);
	uint32_t r = matuwall_alpha_mul((uint8_t)(argb >> 16), opacity);
	uint32_t g = matuwall_alpha_mul((uint8_t)(argb >> 8), opacity);
	uint32_t b = matuwall_alpha_mul((uint8_t)argb, opacity);
	return a << 24 | r << 16 | g << 8 | b;
}

static inline int matuwall_color_hex(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

// Parse "#rrggbb" (opaque) or "#rrggbbaa" into out; out is untouched on failure
static inline bool matuwall_color_parse(
	const char *s, struct matuwall_color *out) {
	if (s == NULL || s[0] != '#') {
		return false;
	}
	size_t len = strlen(s + 1);
	if (len != 6 && len != 8) {
		return false;
	}
	uint32_t v = 0;
	for (size_t i = 1; i <= len; i++) {
		int digit = matuwall_color_hex(s[i]);
		if (digit < 0) {
			return false;
		}
		v = (v << 4) | (uint32_t)digit;
	}
	if (len == 6) {
		out->r = (uint8_t)(v >> 16);
		out->g = (uint8_t)(v >> 8);
		out->b = (uint8_t)v;
		out->a = 0xff;
	} else {
		out->r = (uint8_t)(v >> 24);
		out->g = (uint8_t)(v >> 16);
		out->b = (uint8_t)(v >> 8);
		out->a = (uint8_t)v;
	}
	return true;
}

#endif
