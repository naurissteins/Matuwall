#ifndef MATUWALL_RENDER_FRAME_H
#define MATUWALL_RENDER_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"
#include "render/backdrop.h"
#include "render/color.h"
#include "thumb/worker.h"
#include "wayland/shm.h"

struct matuwall_frame_ring {
	double x;
	double y;
	double width;
	double height;
	uint8_t alpha;
};

struct matuwall_frame_focus {
	size_t index;
	int64_t slot;
	double scale;
};

struct matuwall_frame {
	const struct matuwall_layout *layout;
	const struct matuwall_thumb *thumbs;
	size_t item_count;
	int64_t carousel_slot;
	double scroll;
	struct matuwall_frame_ring rings[1];
	size_t ring_count;
	struct matuwall_frame_focus focuses[2];
	size_t focus_count;
	// Panel geometry in logical, surface-local units
	struct matuwall_rect panel;
	// Surface spans the whole output, so the panel floats on a backdrop
	bool backdrop;
	struct matuwall_backdrop preview;
	bool directory_unavailable;
	// Resolved, never MATUWALL_EDGE_AUTO
	enum matuwall_edge edge;
	double scale;
	uint32_t background;
	uint32_t panel_radius;
	uint32_t tile;
	uint32_t border;
	uint32_t border_width;
	uint32_t shadow;
	uint32_t shadow_width;
	// Maximum focus scale keeps the content clip stable during transitions
	double focus_scale;
	struct matuwall_color ring;
	uint32_t ring_width;
	struct matuwall_color spinner;
	uint8_t spinner_alpha;
};

struct matuwall_damage matuwall_frame_draw(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, uint64_t backdrop_generation);

#endif
