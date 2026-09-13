#ifndef SWEETWALL_GRID_LAYOUT_H
#define SWEETWALL_GRID_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

// Where the grid panel sits, either as a surface anchor or inside a backdrop
enum sweetwall_position {
	SWEETWALL_POSITION_CENTER,
	SWEETWALL_POSITION_LEFT,
	SWEETWALL_POSITION_RIGHT,
	SWEETWALL_POSITION_TOP,
	SWEETWALL_POSITION_BOTTOM,
};

struct sweetwall_rect {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

// Grid metrics in logical (surface-local) units
struct sweetwall_layout {
	uint32_t columns;
	uint32_t spacing;
	uint32_t margin;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t radius;
};

void sweetwall_layout_adapt(const struct sweetwall_layout *configured,
	uint32_t configured_rows, uint32_t available_width,
	uint32_t available_height, struct sweetwall_layout *layout,
	uint32_t *visible_rows);

uint32_t sweetwall_layout_rows(
	const struct sweetwall_layout *layout, size_t count);

struct sweetwall_rect sweetwall_layout_item(
	const struct sweetwall_layout *layout, size_t index);

void sweetwall_layout_surface_size(const struct sweetwall_layout *layout,
	size_t count, uint32_t max_rows, uint32_t *width, uint32_t *height);

// Where the panel sits inside a surface that may be larger than it
struct sweetwall_rect sweetwall_layout_panel(
	const struct sweetwall_layout *layout, size_t count, uint32_t max_rows,
	enum sweetwall_position position, uint32_t surface_width,
	uint32_t surface_height);

size_t sweetwall_layout_hit(const struct sweetwall_layout *layout,
	const struct sweetwall_rect *panel, int32_t scroll, size_t count,
	int32_t x, int32_t y);

#endif
