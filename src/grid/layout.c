#include "grid/layout.h"

uint32_t sweetwall_layout_rows(
	const struct sweetwall_layout *layout, size_t count) {
	if (layout->columns == 0 || count == 0) {
		return 0;
	}
	return (uint32_t)((count + layout->columns - 1) / layout->columns);
}

struct sweetwall_rect sweetwall_layout_item(
	const struct sweetwall_layout *layout, size_t index) {
	if (layout->columns == 0) {
		return (struct sweetwall_rect){0};
	}

	uint32_t column = (uint32_t)(index % layout->columns);
	uint32_t row = (uint32_t)(index / layout->columns);

	return (struct sweetwall_rect){
		.x = (int32_t)(layout->margin +
			       column * (layout->tile_width + layout->spacing)),
		.y = (int32_t)(layout->margin +
			       row * (layout->tile_height + layout->spacing)),
		.width = (int32_t)layout->tile_width,
		.height = (int32_t)layout->tile_height,
	};
}

void sweetwall_layout_surface_size(const struct sweetwall_layout *layout,
	size_t count, uint32_t max_rows, uint32_t *width, uint32_t *height) {
	uint32_t columns = layout->columns;
	if (columns == 0) {
		columns = 1;
	}
	// A short directory should not leave empty columns of padding
	if (count > 0 && count < columns) {
		columns = (uint32_t)count;
	}

	uint32_t rows = sweetwall_layout_rows(layout, count);
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
