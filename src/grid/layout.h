#ifndef MATUWALL_GRID_LAYOUT_H
#define MATUWALL_GRID_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Where the grid panel sits, either as a surface anchor or inside a backdrop
enum matuwall_position {
	MATUWALL_POSITION_CENTER,
	MATUWALL_POSITION_LEFT,
	MATUWALL_POSITION_RIGHT,
	MATUWALL_POSITION_TOP,
	MATUWALL_POSITION_BOTTOM,
};

// what tiles do where the content meets the panel margin
enum matuwall_edge {
	MATUWALL_EDGE_CLIP,
	MATUWALL_EDGE_PEEK,
	MATUWALL_EDGE_FADE,
};

enum matuwall_flow {
	MATUWALL_FLOW_GRID,
	MATUWALL_FLOW_HORIZONTAL,
	MATUWALL_FLOW_VERTICAL,
};

struct matuwall_rect {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct matuwall_layout_rect {
	double x;
	double y;
	double width;
	double height;
};

// Grid metrics in logical (surface-local) units
struct matuwall_layout {
	uint32_t columns;
	uint32_t spacing;
	uint32_t margin;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t radius;
	enum matuwall_flow flow;
};

void matuwall_layout_adapt(const struct matuwall_layout *configured,
	uint32_t configured_rows, uint32_t available_width,
	uint32_t available_height, bool carousel,
	enum matuwall_position position, uint32_t edge_margin,
	struct matuwall_layout *layout, uint32_t *visible_rows);

uint32_t matuwall_layout_rows(
	const struct matuwall_layout *layout, size_t count);

struct matuwall_layout_rect matuwall_layout_slot(
	const struct matuwall_layout *layout, int64_t slot);

size_t matuwall_layout_carousel_index(int64_t slot, size_t count);

double matuwall_layout_scroll(const struct matuwall_layout *layout,
	const struct matuwall_rect *panel, int64_t selected_slot,
	uint32_t first_row);

void matuwall_layout_surface_size(const struct matuwall_layout *layout,
	size_t count, uint32_t max_rows, uint32_t *width, uint32_t *height);

// Where the panel sits inside a surface that may be larger than it
struct matuwall_rect matuwall_layout_panel(const struct matuwall_layout *layout,
	size_t count, uint32_t max_rows, enum matuwall_position position,
	uint32_t edge_margin, uint32_t surface_width, uint32_t surface_height);

size_t matuwall_layout_hit(const struct matuwall_layout *layout,
	const struct matuwall_rect *panel, double scroll, size_t count,
	int64_t *slot, int32_t x, int32_t y);

#endif
