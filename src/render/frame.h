#ifndef SWEETWALL_RENDER_FRAME_H
#define SWEETWALL_RENDER_FRAME_H

#include <stddef.h>

#include "grid/layout.h"
#include "wayland/shm.h"

struct sweetwall_frame {
	const struct sweetwall_layout *layout;
	size_t item_count;
	size_t selected;
	// Logical pixels the grid is scrolled up by
	int32_t scroll;
	uint32_t surface_width;
	uint32_t surface_height;
	double scale;
	uint32_t background;
	uint32_t tile;
	uint32_t ring;
};

void sweetwall_frame_draw(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame);

#endif
