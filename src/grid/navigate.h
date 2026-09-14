#ifndef MATUWALL_GRID_NAVIGATE_H
#define MATUWALL_GRID_NAVIGATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"

// Selection and scroll state
struct matuwall_grid {
	size_t count;
	size_t selected;
	int64_t cursor;
	uint32_t first_row;
};

enum matuwall_move {
	MATUWALL_MOVE_LEFT,
	MATUWALL_MOVE_RIGHT,
	MATUWALL_MOVE_UP,
	MATUWALL_MOVE_DOWN,
	MATUWALL_MOVE_PAGE_UP,
	MATUWALL_MOVE_PAGE_DOWN,
	MATUWALL_MOVE_FIRST,
	MATUWALL_MOVE_LAST,
};

void matuwall_grid_init(struct matuwall_grid *grid, size_t count);

uint32_t matuwall_grid_visible_rows(
	const struct matuwall_layout *layout, uint32_t surface_height);

bool matuwall_grid_move(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height,
	enum matuwall_move move);

bool matuwall_grid_select(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height,
	size_t index, int64_t slot);

bool matuwall_grid_reveal(struct matuwall_grid *grid,
	const struct matuwall_layout *layout, uint32_t surface_height);

#endif
