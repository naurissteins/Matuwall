#ifndef MATUWALL_THUMB_DECODE_H
#define MATUWALL_THUMB_DECODE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include <stddef.h>

struct matuwall_image {
	uint32_t width;
	uint32_t height;
	uint32_t *pixels;
	// nonzero when pixels map a memfd another process can map too
	size_t shared_bytes;
	int shared_fd;
};

enum matuwall_decode_purpose {
	MATUWALL_DECODE_THUMBNAIL,
	// lands in a memfd, so the compositor can show it without a copy
	MATUWALL_DECODE_PREVIEW,
};

// decoders declare their transient peak before allocating it. reserve
// may block, and returns false when the decode should give up
struct matuwall_decode_budget {
	bool (*reserve)(void *user_data, uint64_t bytes);
	void *user_data;
};

// largest decoded or cached image, in pixels
#define MATUWALL_IMAGE_MAX_PIXELS (1u << 26)

bool matuwall_image_dimensions_ok(uint32_t width, uint32_t height);

// cover scaled to exactly target_w x target_h; budget may be NULL
bool matuwall_image_decode(struct matuwall_image *img, const char *path,
	uint32_t target_w, uint32_t target_h,
	enum matuwall_decode_purpose purpose, const atomic_bool *stop,
	const struct matuwall_decode_budget *budget);

// frees heap pixels, or unmaps and closes a memfd
void matuwall_image_free(struct matuwall_image *img);

#endif
