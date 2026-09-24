#ifndef MATUWALL_RENDER_TILES_H
#define MATUWALL_RENDER_TILES_H

#include <stdint.h>

#include "render/draw.h"
#include "render/frame.h"
#include "wayland/shm.h"

struct matuwall_tile_edge {
	double scale;
	// logical offset along the scroll axis
	double shift;
	uint8_t opacity;
};

// carousel edge fade for one slot, identity when the frame does not fade
struct matuwall_tile_edge matuwall_tiles_edge(
	const struct matuwall_frame *frame, int64_t slot);

// Unfocused tiles first, focused tiles and their shadows above
void matuwall_tiles_draw(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip);

#endif
