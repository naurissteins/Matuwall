#ifndef MATUWALL_THUMB_SCALE_H
#define MATUWALL_THUMB_SCALE_H

#include "thumb/decode.h"

void matuwall_cover_crop(uint32_t src_w, uint32_t src_h, uint32_t out_w,
	uint32_t out_h, uint32_t *crop_x, uint32_t *crop_y, uint32_t *crop_w,
	uint32_t *crop_h);

bool matuwall_scale_cover(const struct matuwall_image *src, uint32_t out_w,
	uint32_t out_h, struct matuwall_image *out, const atomic_bool *stop);

#endif
