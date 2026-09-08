#ifndef SWEETWALL_THUMB_DECODE_H
#define SWEETWALL_THUMB_DECODE_H

#include <stdbool.h>
#include <stdint.h>

struct sweetwall_image {
	uint32_t width;
	uint32_t height;
	uint32_t *pixels;
};

bool sweetwall_image_dimensions_ok(uint32_t width, uint32_t height);

// JPEG uses the target as a lower bound; other formats decode fully
bool sweetwall_image_decode(struct sweetwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h);

void sweetwall_image_free(struct sweetwall_image *img);

#endif
