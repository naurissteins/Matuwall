#ifndef SWEETWALL_GRID_NAVIGATE_H
#define SWEETWALL_GRID_NAVIGATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"

// Selection and scroll state
struct sweetwall_grid {
	size_t count;
	size_t selected;
	uint32_t first_row;
};

enum sweetwall_move {
	SWEETWALL_MOVE_LEFT,
	SWEETWALL_MOVE_RIGHT,
	SWEETWALL_MOVE_UP,
	SWEETWALL_MOVE_DOWN,
	SWEETWALL_MOVE_FIRST,
	SWEETWALL_MOVE_LAST,
};

void sweetwall_grid_init(struct sweetwall_grid *grid, size_t count);

uint32_t sweetwall_grid_visible_rows(
	const struct sweetwall_layout *layout, uint32_t surface_height);

bool sweetwall_grid_move(struct sweetwall_grid *grid,
	const struct sweetwall_layout *layout, uint32_t surface_height,
	enum sweetwall_move move);

bool sweetwall_grid_reveal(struct sweetwall_grid *grid,
	const struct sweetwall_layout *layout, uint32_t surface_height);

#endif
