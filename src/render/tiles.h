#ifndef MATUWALL_RENDER_TILES_H
#define MATUWALL_RENDER_TILES_H

#include "render/draw.h"
#include "render/frame.h"
#include "wayland/shm.h"

// Unfocused tiles first, focused tiles and their shadows above
void matuwall_tiles_draw(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip);

#endif
