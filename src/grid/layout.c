#include "grid/layout.h"

#include <math.h>

static uint32_t fit_cells(uint32_t available, uint32_t margin, uint32_t tile,
	uint32_t spacing, uint32_t maximum) {
	if (maximum == 0 || tile == 0) {
		return 1;
	}

	uint64_t margins = (uint64_t)margin * 2;
	if (available <= margins) {
		return 1;
	}
	uint64_t inner = available - margins;
	uint64_t step = (uint64_t)tile + spacing;
	uint64_t count = (inner + spacing) / step;
	if (count == 0) {
		count = 1;
	}
	return count < maximum ? (uint32_t)count : maximum;
}

void matuwall_layout_adapt(const struct matuwall_layout *configured,
	uint32_t configured_rows, uint32_t available_width,
	uint32_t available_height, bool carousel,
	enum matuwall_position position, struct matuwall_layout *layout,
	uint32_t *visible_rows) {
	*layout = *configured;
	if (carousel && (position == MATUWALL_POSITION_LEFT ||
				position == MATUWALL_POSITION_RIGHT)) {
		layout->flow = MATUWALL_FLOW_VERTICAL;
		layout->columns = 1;
		*visible_rows = fit_cells(available_height, configured->margin,
			configured->tile_height, configured->spacing,
			configured->columns);
		return;
	}

	layout->columns = fit_cells(available_width, configured->margin,
		configured->tile_width, configured->spacing,
		configured->columns);
	if (carousel) {
		layout->flow = MATUWALL_FLOW_HORIZONTAL;
		*visible_rows = 1;
		return;
	}

	layout->flow = MATUWALL_FLOW_GRID;
	*visible_rows = fit_cells(available_height, configured->margin,
		configured->tile_height, configured->spacing, configured_rows);
}

uint32_t matuwall_layout_rows(
	const struct matuwall_layout *layout, size_t count) {
	if (layout->columns == 0 || count == 0) {
		return 0;
	}
	return (uint32_t)((count + layout->columns - 1) / layout->columns);
}

struct matuwall_layout_rect matuwall_layout_slot(
	const struct matuwall_layout *layout, int64_t slot) {
	double column =
		layout->flow == MATUWALL_FLOW_HORIZONTAL ? (double)slot : 0;
	double row = layout->flow == MATUWALL_FLOW_VERTICAL ? (double)slot : 0;
	if (layout->flow == MATUWALL_FLOW_GRID && slot >= 0) {
		uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
		column = (double)((uint64_t)slot % columns);
		uint64_t row_index = (uint64_t)slot / columns;
		row = (double)row_index;
	}

	return (struct matuwall_layout_rect){
		.x = layout->margin +
		     column * (layout->tile_width + layout->spacing),
		.y = layout->margin +
		     row * (layout->tile_height + layout->spacing),
		.width = layout->tile_width,
		.height = layout->tile_height,
	};
}

size_t matuwall_layout_carousel_index(int64_t slot, size_t count) {
	if (count == 0) {
		return SIZE_MAX;
	}
	if (slot >= 0) {
		return (size_t)slot % count;
	}
	uint64_t magnitude = (uint64_t)(-(slot + 1)) + 1;
	size_t remainder = (size_t)(magnitude % count);
	return remainder == 0 ? 0 : count - remainder;
}

double matuwall_layout_scroll(const struct matuwall_layout *layout,
	const struct matuwall_rect *panel, int64_t selected_slot,
	uint32_t first_row) {
	if (layout->flow == MATUWALL_FLOW_GRID) {
		return (double)first_row *
		       (double)(layout->tile_height + layout->spacing);
	}

	struct matuwall_layout_rect item =
		matuwall_layout_slot(layout, selected_slot);
	if (layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		return (double)item.x + (double)item.width / 2.0 -
		       (double)panel->width / 2.0;
	}
	return (double)item.y + (double)item.height / 2.0 -
	       (double)panel->height / 2.0;
}

void matuwall_layout_surface_size(const struct matuwall_layout *layout,
	size_t count, uint32_t max_rows, uint32_t *width, uint32_t *height) {
	uint32_t columns = layout->columns;
	if (columns == 0) {
		columns = 1;
	}
	// A short grid should not leave empty columns of padding
	if (layout->flow == MATUWALL_FLOW_GRID && count > 0 &&
		count < columns) {
		columns = (uint32_t)count;
	}

	uint32_t rows = layout->flow == MATUWALL_FLOW_VERTICAL
				? max_rows
				: matuwall_layout_rows(layout, count);
	if (rows == 0) {
		rows = 1;
	}
	if (max_rows > 0 && rows > max_rows) {
		rows = max_rows;
	}

	*width = layout->margin * 2 + columns * layout->tile_width +
		 (columns - 1) * layout->spacing;
	*height = layout->margin * 2 + rows * layout->tile_height +
		  (rows - 1) * layout->spacing;
}

// Clamp an edge offset to the space the surface actually leaves free
static int32_t edge_gap(int32_t gap, int32_t free_space) {
	if (free_space <= 0) {
		return 0;
	}
	return gap < free_space ? gap : free_space;
}

struct matuwall_rect matuwall_layout_panel(const struct matuwall_layout *layout,
	size_t count, uint32_t max_rows, enum matuwall_position position,
	uint32_t surface_width, uint32_t surface_height) {
	uint32_t width;
	uint32_t height;
	matuwall_layout_surface_size(layout, count, max_rows, &width, &height);
	if (width > surface_width) {
		width = surface_width;
	}
	if (height > surface_height) {
		height = surface_height;
	}

	int32_t free_x = (int32_t)(surface_width - width);
	int32_t free_y = (int32_t)(surface_height - height);
	int32_t gap = (int32_t)layout->margin;

	struct matuwall_rect rect = {
		.x = free_x / 2,
		.y = free_y / 2,
		.width = (int32_t)width,
		.height = (int32_t)height,
	};

	switch (position) {
	case MATUWALL_POSITION_LEFT:
		rect.x = edge_gap(gap, free_x);
		break;
	case MATUWALL_POSITION_RIGHT:
		rect.x = free_x - edge_gap(gap, free_x);
		break;
	case MATUWALL_POSITION_TOP:
		rect.y = edge_gap(gap, free_y);
		break;
	case MATUWALL_POSITION_BOTTOM:
		rect.y = free_y - edge_gap(gap, free_y);
		break;
	case MATUWALL_POSITION_CENTER:
		break;
	}
	return rect;
}

size_t matuwall_layout_hit(const struct matuwall_layout *layout,
	const struct matuwall_rect *panel, double scroll, size_t count,
	int64_t *slot, int32_t x, int32_t y) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	double stride_x = layout->tile_width + layout->spacing;
	double stride_y = layout->tile_height + layout->spacing;

	if (x < panel->x || y < panel->y || x >= panel->x + panel->width ||
		y >= panel->y + panel->height) {
		return SIZE_MAX;
	}

	// Translate the on-screen point into unscrolled content space
	double cx = x - panel->x - (int32_t)layout->margin;
	double cy = y - panel->y - (int32_t)layout->margin;
	if (layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		cx += scroll;
	} else {
		cy += scroll;
	}
	if (layout->flow == MATUWALL_FLOW_GRID && (cx < 0 || cy < 0)) {
		return SIZE_MAX;
	}

	int64_t col = (int64_t)floor(cx / stride_x);
	int64_t row = (int64_t)floor(cy / stride_y);
	if (layout->flow != MATUWALL_FLOW_HORIZONTAL &&
		(col < 0 || (uint64_t)col >= columns)) {
		return SIZE_MAX;
	}
	if (layout->flow == MATUWALL_FLOW_HORIZONTAL && row != 0) {
		return SIZE_MAX;
	}
	// Reject points that fall in the spacing gap, not on a tile
	if (cx - (double)col * stride_x >= layout->tile_width) {
		return SIZE_MAX;
	}
	if (cy - (double)row * stride_y >= layout->tile_height) {
		return SIZE_MAX;
	}

	if (layout->flow != MATUWALL_FLOW_GRID) {
		int64_t hit_slot =
			layout->flow == MATUWALL_FLOW_HORIZONTAL ? col : row;
		if (slot != NULL) {
			*slot = hit_slot;
		}
		return matuwall_layout_carousel_index(hit_slot, count);
	}

	size_t index = (size_t)row * columns + (size_t)col;
	if (slot != NULL) {
		*slot = (int64_t)index;
	}
	return index < count ? index : SIZE_MAX;
}
