#ifndef MATUWALL_THUMB_DECODE_H
#define MATUWALL_THUMB_DECODE_H

#include <stdbool.h>
#include <stdint.h>

struct matuwall_image {
	uint32_t width;
	uint32_t height;
	uint32_t *pixels;
};

bool matuwall_image_dimensions_ok(uint32_t width, uint32_t height);

// JPEG uses the target as a lower bound; other formats decode fully
bool matuwall_image_decode(struct matuwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h);

void matuwall_image_free(struct matuwall_image *img);

#endif
