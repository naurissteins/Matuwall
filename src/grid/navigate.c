#include "grid/navigate.h"

#include <stdbool.h>

void matuwall_grid_init(struct matuwall_grid *grid, size_t count) {
	*grid = (struct matuwall_grid){.count = count};
}

uint32_t matuwall_grid_visible_rows(
	const struct matuwall_layout *layout, uint32_t surface_height) {
	uint32_t step = layout->tile_height + layout->spacing;
	uint32_t inner = surface_height > layout->margin * 2
				 ? surface_height - layout->margin * 2
				 : 0;
	// The last visible row needs no trailing spacing
	uint32_t rows = step > 0 ? (inner + layout->spacing) / step : 0;
	return rows == 0 ? 1 : rows;
}

static uint32_t row_of(const struct matuwall_layout *layout, size_t index) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	return (uint32_t)(index / columns);
}

static uint32_t shift_row(
	uint32_t row, uint32_t amount, uint32_t last, bool forward) {
	if (row > last) {
		row = last;
	}
	if (!forward) {
		return amount < row ? row - amount : 0;
	}
	return amount < last - row ? row + amount : last;
}

static size_t move_page(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height,
	bool forward) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	uint32_t visible = matuwall_grid_visible_rows(layout, surface_height);
	uint32_t last_row = row_of(layout, grid->count - 1);
	uint32_t row = row_of(layout, grid->selected);
	uint32_t column = (uint32_t)(grid->selected % columns);

	uint32_t target_row = shift_row(row, visible, last_row, forward);
	size_t selected = (size_t)target_row * columns + column;
	if (selected >= grid->count) {
		selected = grid->count - 1;
	}

	uint32_t total_rows = last_row + 1;
	uint32_t max_first = total_rows > visible ? total_rows - visible : 0;
	grid->first_row =
		shift_row(grid->first_row, visible, max_first, forward);
	return selected;
}

// Keep the selected row inside the viewport and never scroll past the end
static bool scroll_into_view(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height) {
	if (layout->flow != MATUWALL_FLOW_GRID) {
		if (grid->first_row == 0) {
			return false;
		}
		grid->first_row = 0;
		return true;
	}

	uint32_t visible = matuwall_grid_visible_rows(layout, surface_height);
	uint32_t total = matuwall_layout_rows(layout, grid->count);
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

// +1 or -1 along a carousel's scroll axis, 0 for any other move
static int carousel_step(enum matuwall_flow flow, enum matuwall_move move) {
	if (flow == MATUWALL_FLOW_GRID) {
		return 0;
	}
	bool horizontal = flow == MATUWALL_FLOW_HORIZONTAL;
	if (move == (horizontal ? MATUWALL_MOVE_RIGHT : MATUWALL_MOVE_DOWN)) {
		return 1;
	}
	if (move == (horizontal ? MATUWALL_MOVE_LEFT : MATUWALL_MOVE_UP)) {
		return -1;
	}
	return 0;
}

static size_t grid_step(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height,
	enum matuwall_move move) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	size_t last = grid->count - 1;
	size_t selected = grid->selected;
	switch (move) {
	case MATUWALL_MOVE_LEFT:
		// Linear across row boundaries, like an icon grid
		return selected > 0 ? selected - 1 : 0;
	case MATUWALL_MOVE_RIGHT:
		return selected < last ? selected + 1 : selected;
	case MATUWALL_MOVE_UP:
		return selected >= columns ? selected - columns : selected;
	case MATUWALL_MOVE_DOWN:
		if (selected + columns <= last) {
			return selected + columns;
		}
		// short final row still deserves to be reachable
		return row_of(layout, selected) < row_of(layout, last)
			       ? last
			       : selected;
	case MATUWALL_MOVE_PAGE_UP:
		return move_page(grid, layout, surface_height, false);
	case MATUWALL_MOVE_PAGE_DOWN:
		return move_page(grid, layout, surface_height, true);
	default:
		return selected;
	}
}

bool matuwall_grid_move(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height,
	enum matuwall_move move) {
	if (grid->count == 0) {
		return false;
	}

	size_t last = grid->count - 1;
	size_t selected = grid->selected;
	size_t previous_selected = grid->selected;
	int64_t previous_cursor = grid->cursor;
	uint32_t first_row = grid->first_row;

	int step = carousel_step(layout->flow, move);
	if (step != 0) {
		if (step < 0 ? grid->cursor > INT64_MIN
			     : grid->cursor < INT64_MAX) {
			grid->cursor += step;
		}
		selected = matuwall_layout_carousel_index(
			grid->cursor, grid->count);
	} else if (move == MATUWALL_MOVE_FIRST) {
		selected = 0;
		grid->cursor = 0;
	} else if (move == MATUWALL_MOVE_LAST) {
		selected = last;
		grid->cursor = (int64_t)last;
	} else if (layout->flow == MATUWALL_FLOW_GRID) {
		selected = grid_step(grid, layout, surface_height, move);
	}

	grid->selected = selected;
	if (layout->flow == MATUWALL_FLOW_GRID) {
		grid->cursor = (int64_t)selected;
	}
	scroll_into_view(grid, layout, surface_height);
	return selected != previous_selected ||
	       grid->cursor != previous_cursor || grid->first_row != first_row;
}

bool matuwall_grid_select(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height,
	size_t index, int64_t slot) {
	if (grid->count == 0 || index >= grid->count) {
		return false;
	}
	bool moved = index != grid->selected;
	if (layout->flow != MATUWALL_FLOW_GRID &&
		matuwall_layout_carousel_index(slot, grid->count) == index) {
		moved = moved || slot != grid->cursor;
		grid->cursor = slot;
	} else {
		grid->cursor = (int64_t)index;
	}
	grid->selected = index;
	bool scrolled = scroll_into_view(grid, layout, surface_height);
	return moved || scrolled;
}

bool matuwall_grid_reveal(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height) {
	if (grid->count == 0) {
		return false;
	}
	return scroll_into_view(grid, layout, surface_height);
}
