#ifndef MATUWALL_THUMB_DECODE_H
#define MATUWALL_THUMB_DECODE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct matuwall_image {
	uint32_t width;
	uint32_t height;
	uint32_t *pixels;
};

enum matuwall_decode_purpose {
	MATUWALL_DECODE_THUMBNAIL,
	MATUWALL_DECODE_PREVIEW,
};

bool matuwall_image_dimensions_ok(uint32_t width, uint32_t height);

bool matuwall_image_decode(struct matuwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose, const atomic_bool *stop);

void matuwall_image_free(struct matuwall_image *img);

#endif
