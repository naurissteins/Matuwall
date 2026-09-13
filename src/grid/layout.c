#include "grid/layout.h"

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
	uint32_t available_height, struct matuwall_layout *layout,
	uint32_t *visible_rows) {
	*layout = *configured;
	layout->columns = fit_cells(available_width, configured->margin,
		configured->tile_width, configured->spacing,
		configured->columns);
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

struct matuwall_rect matuwall_layout_item(
	const struct matuwall_layout *layout, size_t index) {
	if (layout->columns == 0) {
		return (struct matuwall_rect){0};
	}

	uint32_t column = (uint32_t)(index % layout->columns);
	uint32_t row = (uint32_t)(index / layout->columns);

	return (struct matuwall_rect){
		.x = (int32_t)(layout->margin +
			       column * (layout->tile_width + layout->spacing)),
		.y = (int32_t)(layout->margin +
			       row * (layout->tile_height + layout->spacing)),
		.width = (int32_t)layout->tile_width,
		.height = (int32_t)layout->tile_height,
	};
}

void matuwall_layout_surface_size(const struct matuwall_layout *layout,
	size_t count, uint32_t max_rows, uint32_t *width, uint32_t *height) {
	uint32_t columns = layout->columns;
	if (columns == 0) {
		columns = 1;
	}
	// A short directory should not leave empty columns of padding
	if (count > 0 && count < columns) {
		columns = (uint32_t)count;
	}

	uint32_t rows = matuwall_layout_rows(layout, count);
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
	const struct matuwall_rect *panel, int32_t scroll, size_t count,
	int32_t x, int32_t y) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	int32_t stride_x = (int32_t)(layout->tile_width + layout->spacing);
	int32_t stride_y = (int32_t)(layout->tile_height + layout->spacing);

	if (x < panel->x || y < panel->y || x >= panel->x + panel->width ||
		y >= panel->y + panel->height) {
		return SIZE_MAX;
	}

	// Translate the on-screen point into unscrolled content space
	int32_t cx = x - panel->x - (int32_t)layout->margin;
	int32_t cy = y - panel->y - (int32_t)layout->margin + scroll;
	if (cx < 0 || cy < 0) {
		return SIZE_MAX;
	}

	uint32_t col = (uint32_t)(cx / stride_x);
	uint32_t row = (uint32_t)(cy / stride_y);
	if (col >= columns) {
		return SIZE_MAX;
	}
	// Reject points that fall in the spacing gap, not on a tile
	if (cx - (int32_t)col * stride_x >= (int32_t)layout->tile_width) {
		return SIZE_MAX;
	}
	if (cy - (int32_t)row * stride_y >= (int32_t)layout->tile_height) {
		return SIZE_MAX;
	}

	size_t index = (size_t)row * columns + col;
	return index < count ? index : SIZE_MAX;
}
