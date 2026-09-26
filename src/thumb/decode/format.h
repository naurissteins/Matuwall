#ifndef MATUWALL_THUMB_DECODE_FORMAT_H
#define MATUWALL_THUMB_DECODE_FORMAT_H

// shared by thumb/decode.c and the per-format decoders, nothing else

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "thumb/decode.h"

#define MAX_THUMBNAIL_PIXELS (1u << 24)
// refuse decodes whose transient memory estimate passes this
#define DECODE_MEMORY_LIMIT ((uint64_t)512 * 1024 * 1024)

// one decode's target and limits, shared by every format; the target
// already passed decode_dimensions_ok in matuwall_image_decode
struct decode_job {
	uint32_t target_w;
	uint32_t target_h;
	enum matuwall_decode_purpose purpose;
	const atomic_bool *stop;
	const struct matuwall_decode_budget *budget;
};

static inline bool stop_requested(const atomic_bool *stop) {
	return stop != NULL && atomic_load_explicit(stop, memory_order_relaxed);
}

// called once per decode, before its large allocations
static inline bool reserve(const struct decode_job *job, uint64_t bytes) {
	if (bytes > DECODE_MEMORY_LIMIT) {
		return false;
	}
	return job->budget == NULL ||
	       job->budget->reserve(job->budget->user_data, bytes);
}

static inline uint64_t target_bytes(const struct decode_job *job) {
	return (uint64_t)job->target_w * job->target_h * sizeof(uint32_t);
}

// the decoded output, freed through matuwall_image_free
static inline uint32_t *alloc_target(const struct decode_job *job) {
	uint64_t bytes = target_bytes(job);
	// unreachable after dispatch, but no path may reach malloc(0)
	return bytes == 0 ? NULL : malloc((size_t)bytes);
}

static inline bool decode_dimensions_ok(
	uint32_t width, uint32_t height, enum matuwall_decode_purpose purpose) {
	if (!matuwall_image_dimensions_ok(width, height)) {
		return false;
	}
	uint64_t limit = purpose == MATUWALL_DECODE_THUMBNAIL
				 ? MAX_THUMBNAIL_PIXELS
				 : MATUWALL_IMAGE_MAX_PIXELS;
	return (uint64_t)width * height <= limit;
}

// fp is at the start of the file; img is left empty on failure
bool matuwall_decode_jpeg(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job);
bool matuwall_decode_png(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job);
bool matuwall_decode_webp(
	FILE *fp, struct matuwall_image *img, const struct decode_job *job);

#endif
