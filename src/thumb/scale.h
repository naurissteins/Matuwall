#ifndef MATUWALL_THUMB_SCALE_H
#define MATUWALL_THUMB_SCALE_H

#include "thumb/decode.h"

bool matuwall_scale_cover(const struct matuwall_image *src, uint32_t out_w,
	uint32_t out_h, struct matuwall_image *out);

#endif
