#include "grid/navigate.h"

#include <stdbool.h>

void sweetwall_grid_init(struct sweetwall_grid *grid, size_t count) {
	*grid = (struct sweetwall_grid){.count = count};
}

uint32_t sweetwall_grid_visible_rows(
	const struct sweetwall_layout *layout, uint32_t surface_height) {
	uint32_t step = layout->tile_height + layout->spacing;
	uint32_t inner = surface_height > layout->margin * 2
				 ? surface_height - layout->margin * 2
				 : 0;
	// The last visible row needs no trailing spacing
	uint32_t rows = step > 0 ? (inner + layout->spacing) / step : 0;
	return rows == 0 ? 1 : rows;
}

static uint32_t row_of(const struct sweetwall_layout *layout, size_t index) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	return (uint32_t)(index / columns);
}

// Keep the selected row inside the viewport and never scroll past the end
static bool scroll_into_view(struct sweetwall_grid *grid,
	const struct sweetwall_layout *layout, uint32_t surface_height) {
	uint32_t visible = sweetwall_grid_visible_rows(layout, surface_height);
	uint32_t total = sweetwall_layout_rows(layout, grid->count);
	uint32_t first = grid->first_row;

	uint32_t row = row_of(layout, grid->selected);
	if (row < first) {
		first = row;
	} else if (row >= first + visible) {
		first = row - visible + 1;
	}

	uint32_t max_first = total > visible ? total - visible : 0;
	if (first > max_first) {
		first = max_first;
	}

	if (first == grid->first_row) {
		return false;
	}
	grid->first_row = first;
	return true;
}

bool sweetwall_grid_move(struct sweetwall_grid *grid,
	const struct sweetwall_layout *layout, uint32_t surface_height,
	enum sweetwall_move move) {
	if (grid->count == 0) {
		return false;
	}

	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	size_t last = grid->count - 1;
	size_t selected = grid->selected;

	switch (move) {
	case SWEETWALL_MOVE_LEFT:
		// Linear across row boundaries, like an icon grid
		if (selected > 0) {
			selected--;
		}
		break;
	case SWEETWALL_MOVE_RIGHT:
		if (selected < last) {
			selected++;
		}
		break;
	case SWEETWALL_MOVE_UP:
		if (selected >= columns) {
			selected -= columns;
		}
		break;
	case SWEETWALL_MOVE_DOWN:
		if (selected + columns <= last) {
			selected += columns;
		} else if (row_of(layout, selected) < row_of(layout, last)) {
			// A short final row still deserves to be reachable
			selected = last;
		}
		break;
	case SWEETWALL_MOVE_FIRST:
		selected = 0;
		break;
	case SWEETWALL_MOVE_LAST:
		selected = last;
		break;
	}

	bool moved = selected != grid->selected;
	grid->selected = selected;

	bool scrolled = scroll_into_view(grid, layout, surface_height);
	return moved || scrolled;
}

bool sweetwall_grid_select(struct sweetwall_grid *grid,
	const struct sweetwall_layout *layout, uint32_t surface_height,
	size_t index) {
	if (grid->count == 0 || index >= grid->count) {
		return false;
	}
	bool moved = index != grid->selected;
	grid->selected = index;
	bool scrolled = scroll_into_view(grid, layout, surface_height);
	return moved || scrolled;
}

bool sweetwall_grid_reveal(struct sweetwall_grid *grid,
	const struct sweetwall_layout *layout, uint32_t surface_height) {
	if (grid->count == 0) {
		return false;
	}
	return scroll_into_view(grid, layout, surface_height);
}
