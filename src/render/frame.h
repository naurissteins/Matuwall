#ifndef SWEETWALL_RENDER_FRAME_H
#define SWEETWALL_RENDER_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"
#include "render/color.h"
#include "thumb/worker.h"
#include "wayland/shm.h"

struct sweetwall_frame {
	const struct sweetwall_layout *layout;
	const struct sweetwall_thumb *thumbs;
	size_t item_count;
	size_t selected;
	int32_t scroll;
	// Panel geometry in logical, surface-local units
	struct sweetwall_rect panel;
	// Surface spans the whole output, so the panel floats on a backdrop
	bool backdrop;
	// Wallpaper filling the backdrop; NULL leaves the desktop showing
	const uint32_t *preview;
	uint32_t preview_width;
	uint32_t preview_height;
	double scale;
	uint32_t background;
	uint32_t tile;
	uint32_t ring;
	uint32_t ring_width;
	struct sweetwall_color spinner;
	uint8_t spinner_alpha;
};

void sweetwall_frame_draw(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame);

#endif
