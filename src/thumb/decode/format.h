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

bool matuwall_image_alloc_shared(
	struct matuwall_image *img, uint32_t width, uint32_t height);

// the decoded output at the target size; false leaves img empty
static inline bool alloc_target(
	struct matuwall_image *img, const struct decode_job *job) {
	uint64_t bytes = target_bytes(job);
	// unreachable after dispatch, but no path may reach malloc(0)
	if (bytes == 0) {
		return false;
	}
	if (job->purpose == MATUWALL_DECODE_PREVIEW) {
		return matuwall_image_alloc_shared(
			img, job->target_w, job->target_h);
	}
	img->pixels = malloc((size_t)bytes);
	if (img->pixels == NULL) {
		return false;
	}
	img->width = job->target_w;
	img->height = job->target_h;
	return true;
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
