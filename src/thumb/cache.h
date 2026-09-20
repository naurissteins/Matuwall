#ifndef MATUWALL_THUMB_CACHE_H
#define MATUWALL_THUMB_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thumb/decode.h"

struct matuwall_cache;

struct matuwall_cache_key {
	char filename[640];
	// Borrowed from the scan until the worker pool stops
	const char *source_path;
	uint32_t source_path_length;
	int64_t mtime_sec;
	uint32_t mtime_nsec;
	int64_t source_size;
	uint32_t target_w;
	uint32_t target_h;
};

bool matuwall_cache_dir(char *out, size_t out_size);

struct matuwall_cache *matuwall_cache_create(void);
void matuwall_cache_destroy(struct matuwall_cache *cache);

bool matuwall_cache_key(const struct matuwall_cache *cache,
	const char *source_path, uint32_t target_w, uint32_t target_h,
	struct matuwall_cache_key *out);

bool matuwall_cache_read(
	const struct matuwall_cache_key *key, struct matuwall_image *img);

void matuwall_cache_write(
	const struct matuwall_cache_key *key, const struct matuwall_image *img);

bool matuwall_cache_clear(size_t *removed, char *err, size_t err_size);

#endif
