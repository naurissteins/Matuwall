#ifndef SWEETWALL_THUMB_CACHE_H
#define SWEETWALL_THUMB_CACHE_H

#include <stddef.h>

#include "thumb/decode.h"

bool sweetwall_cache_key(const char *source_path, uint32_t target_w,
	uint32_t target_h, char *out, size_t out_size);

bool sweetwall_cache_read(const char *key, struct sweetwall_image *img);

void sweetwall_cache_write(const char *key, const struct sweetwall_image *img);

#endif
