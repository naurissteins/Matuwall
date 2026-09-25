#ifndef MATUWALL_RENDER_BACKDROP_H
#define MATUWALL_RENDER_BACKDROP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wayland/shm.h"

// Backdrop pixels under the panel, so the full image can be freed
// matuwall_backdrop_draw fills and grows it, the owner frees pixels
struct matuwall_backdrop_patch {
	uint32_t *pixels;
	size_t capacity;
	struct matuwall_damage rect;
	uint32_t buffer_width;
	uint32_t buffer_height;
	uint64_t generation;
	bool valid;
	// frame had neither the image nor a matching patch for the backdrop
	bool missed;
};

// wallpaper behind the panel, with no image and no patch the desktop shows
struct matuwall_backdrop {
	// freed once the patch and every buffer hold it
	const uint32_t *image;
	uint32_t width;
	uint32_t height;
	// both optional, together they stand in for a freed image
	struct matuwall_backdrop_patch *patch;
	const struct matuwall_buffer *sibling;
};

// paints damage in buffer pixels, false when it was left transparent
bool matuwall_backdrop_draw(struct matuwall_buffer *buffer,
	const struct matuwall_backdrop *backdrop, struct matuwall_damage damage,
	struct matuwall_damage panel, uint64_t generation, bool full);

#endif
