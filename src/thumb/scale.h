#ifndef SWEETWALL_THUMB_SCALE_H
#define SWEETWALL_THUMB_SCALE_H

#include "thumb/decode.h"

bool sweetwall_scale_cover(const struct sweetwall_image *src, uint32_t out_w,
	uint32_t out_h, struct sweetwall_image *out);

#endif
