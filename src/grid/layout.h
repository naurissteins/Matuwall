#ifndef SWEETWALL_GRID_LAYOUT_H
#define SWEETWALL_GRID_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

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

uint32_t sweetwall_layout_rows(
	const struct sweetwall_layout *layout, size_t count);

struct sweetwall_rect sweetwall_layout_item(
	const struct sweetwall_layout *layout, size_t index);

void sweetwall_layout_surface_size(const struct sweetwall_layout *layout,
	size_t count, uint32_t max_rows, uint32_t *width, uint32_t *height);

size_t sweetwall_layout_hit(const struct sweetwall_layout *layout,
	uint32_t first_row, size_t count, int32_t x, int32_t y);

#endif
