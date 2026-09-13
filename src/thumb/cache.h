#ifndef MATUWALL_THUMB_CACHE_H
#define MATUWALL_THUMB_CACHE_H

#include <stddef.h>

#include "thumb/decode.h"

bool matuwall_cache_dir(char *out, size_t out_size);

bool matuwall_cache_key(const char *source_path, uint32_t target_w,
	uint32_t target_h, char *out, size_t out_size);

bool matuwall_cache_read(const char *key, struct matuwall_image *img);

void matuwall_cache_write(const char *key, const struct matuwall_image *img);

bool matuwall_cache_clear(size_t *removed, char *err, size_t err_size);

#endif
