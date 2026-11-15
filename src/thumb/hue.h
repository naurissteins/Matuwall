#ifndef MATUWALL_THUMB_HUE_H
#define MATUWALL_THUMB_HUE_H

#include <stdint.h>

// sorts after every computed key
#define MATUWALL_HUE_UNKNOWN UINT32_MAX

// colour sort key of opaque ARGB8888 pixels: dominant hue from red, then
// lightness; mostly grey images follow every hue, light to dark
uint32_t matuwall_hue_key(
	const uint32_t *pixels, uint32_t width, uint32_t height);

#endif
