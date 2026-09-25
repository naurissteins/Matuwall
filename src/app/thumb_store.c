#include "app/thumb_store.h"

#include <stdint.h>
#include <stdlib.h>

#include "app/app.h"
#include "thumb/worker.h"
#include "util/log.h"

#define MAX_RESIDENT_BYTES ((size_t)64 * 1024 * 1024)

static bool thumbnail_bytes(uint32_t width, uint32_t height, size_t *bytes) {
	if (width == 0 || height == 0 ||
		(size_t)width > SIZE_MAX / height / sizeof(uint32_t)) {
		return false;
	}
	*bytes = (size_t)width * height * sizeof(uint32_t);
	return true;
}

bool matuwall_thumb_store_set_target(
	struct matuwall_app *app, uint32_t width, uint32_t height) {
	return thumbnail_bytes(width, height, &app->thumb_target_bytes);
}

size_t matuwall_thumb_store_lookahead(
	const struct matuwall_app *app, size_t visible) {
	if (app->thumb_target_bytes == 0) {
		return visible;
	}
	size_t slots = MAX_RESIDENT_BYTES / app->thumb_target_bytes;
	if (slots <= visible) {
		return 0;
	}
	size_t lookahead = (slots - visible) / 2;
	return lookahead < visible ? lookahead : visible;
}

void matuwall_thumb_store_visible_ranges(const struct matuwall_app *app,
	size_t *first, size_t *end, size_t *wrap_end) {
	size_t columns = app->layout.columns;
	if (columns == 0) {
		columns = 1;
	}

	if (app->layout.flow != MATUWALL_FLOW_GRID) {
		size_t visible =
			app->layout.flow == MATUWALL_FLOW_HORIZONTAL
				? columns
				: matuwall_grid_visible_rows(&app->layout,
					  (uint32_t)app->panel.height);
		if (visible > app->scan.count) {
			visible = app->scan.count;
		}
		int64_t before = (int64_t)(visible / 2);
		int64_t start_slot = app->grid.cursor >= INT64_MIN + before
					     ? app->grid.cursor - before
					     : INT64_MIN;
		*first = matuwall_layout_carousel_index(
			start_slot, app->scan.count);
		size_t until_end = app->scan.count - *first;
		if (visible <= until_end) {
			*end = *first + visible;
			*wrap_end = 0;
		} else {
			*end = app->scan.count;
			*wrap_end = visible - until_end;
		}
		return;
	}

	*first = (size_t)app->grid.first_row * columns;
	if (*first >= app->scan.count) {
		*first = app->scan.count;
		*end = app->scan.count;
		*wrap_end = 0;
		return;
	}

	uint32_t height =
		app->panel.height > 0 ? (uint32_t)app->panel.height : 0;
	size_t rows = matuwall_grid_visible_rows(&app->layout, height);
	size_t count = rows * columns;
	size_t remaining = app->scan.count - *first;
	*end = *first + (count < remaining ? count : remaining);
	*wrap_end = 0;
}

// running animation repaints every frame, only the rest view counts
bool matuwall_thumb_store_drawable(
	const struct matuwall_app *app, size_t index) {
	const struct matuwall_layout *layout = &app->layout;
	double scroll = matuwall_layout_scroll(
		layout, &app->panel, app->grid.cursor, app->grid.first_row);
	if (layout->flow == MATUWALL_FLOW_GRID) {
		return matuwall_layout_slot_reaches_panel(
			layout, &app->panel, scroll, (int64_t)index);
	}

	size_t count = app->thumb_count;
	int64_t cursor = app->grid.cursor;
	if (index >= count || cursor > INT64_MAX - (int64_t)count ||
		cursor < INT64_MIN + (int64_t)count) {
		return true;
	}
	size_t current = matuwall_layout_carousel_index(cursor, count);
	int64_t ahead = (int64_t)((index + count - current) % count);
	// the nearest copies of index on either side of the cursor
	return matuwall_layout_slot_reaches_panel(
		       layout, &app->panel, scroll, cursor + ahead) ||
	       matuwall_layout_slot_reaches_panel(layout, &app->panel, scroll,
		       cursor + ahead - (int64_t)count);
}

static bool in_visible_range(
	size_t index, size_t first, size_t end, size_t wrap_end) {
	return (index >= first && index < end) || index < wrap_end;
}

bool matuwall_thumb_store_in_window(const struct matuwall_app *app,
	size_t index, size_t first, size_t end, size_t wrap_end) {
	size_t visible = end - first + wrap_end;
	if (visible == 0 || index >= app->thumb_count) {
		return false;
	}
	size_t lookahead = matuwall_thumb_store_lookahead(app, visible);
	if (app->layout.flow == MATUWALL_FLOW_GRID) {
		size_t low = first > lookahead ? first - lookahead : 0;
		size_t remaining = app->thumb_count - end;
		size_t high =
			end + (lookahead < remaining ? lookahead : remaining);
		return index >= low && index < high;
	}

	size_t span = visible + lookahead * 2;
	if (span >= app->thumb_count) {
		return true;
	}
	size_t start =
		(first + app->thumb_count - lookahead) % app->thumb_count;
	size_t offset = (index + app->thumb_count - start) % app->thumb_count;
	return offset < span;
}

static void unload_thumbnail(struct matuwall_app *app, size_t index) {
	struct matuwall_thumb *thumb = &app->thumbs[index];
	if (thumb->state != MATUWALL_THUMB_READY) {
		return;
	}

	size_t bytes = 0;
	if (thumbnail_bytes(thumb->width, thumb->height, &bytes) &&
		bytes <= app->thumb_resident_bytes) {
		app->thumb_resident_bytes -= bytes;
	} else {
		app->thumb_resident_bytes = 0;
	}
	free(thumb->pixels);
	*thumb = (struct matuwall_thumb){0};
	app->thumb_evicted++;
}

void matuwall_thumb_store_evict_outside(
	struct matuwall_app *app, size_t first, size_t end, size_t wrap_end) {
	for (size_t i = 0; i < app->thumb_count; i++) {
		if (!matuwall_thumb_store_in_window(
			    app, i, first, end, wrap_end)) {
			unload_thumbnail(app, i);
		}
	}
}

static bool evict_one(struct matuwall_app *app, size_t preserve, size_t first,
	size_t end, size_t wrap_end, bool visible) {
	for (size_t i = 0; i < app->thumb_count; i++) {
		if (i == preserve || i == app->grid.selected ||
			app->thumbs[i].state != MATUWALL_THUMB_READY ||
			in_visible_range(i, first, end, wrap_end) != visible) {
			continue;
		}
		unload_thumbnail(app, i);
		return true;
	}
	return false;
}

static void make_room(struct matuwall_app *app, size_t bytes, size_t preserve,
	size_t first, size_t end, size_t wrap_end) {
	while (bytes > MAX_RESIDENT_BYTES - app->thumb_resident_bytes) {
		if (evict_one(app, preserve, first, end, wrap_end, false)) {
			continue;
		}
		if (!evict_one(app, preserve, first, end, wrap_end, true)) {
			break;
		}
	}
}

void matuwall_thumb_store_accept(struct matuwall_app *app,
	const struct matuwall_thumb_result *result, size_t first, size_t end,
	size_t wrap_end) {
	struct matuwall_thumb *thumb = &app->thumbs[result->index];
	if (!matuwall_thumb_store_in_window(
		    app, result->index, first, end, wrap_end)) {
		free(result->pixels);
		thumb->state = MATUWALL_THUMB_UNLOADED;
		app->thumb_discarded++;
		return;
	}

	size_t bytes;
	if (!thumbnail_bytes(result->width, result->height, &bytes) ||
		bytes > MAX_RESIDENT_BYTES) {
		free(result->pixels);
		thumb->state = MATUWALL_THUMB_FAILED;
		app->thumb_failed++;
		matuwall_log_warn("thumbnail",
			"thumbnail exceeds the resident memory limit: %s",
			app->scan.paths[result->index]);
		return;
	}

	make_room(app, bytes, result->index, first, end, wrap_end);
	if (bytes > MAX_RESIDENT_BYTES - app->thumb_resident_bytes) {
		free(result->pixels);
		thumb->state = MATUWALL_THUMB_UNLOADED;
		app->thumb_discarded++;
		return;
	}

	thumb->state = MATUWALL_THUMB_READY;
	thumb->pixels = result->pixels;
	thumb->width = result->width;
	thumb->height = result->height;
	app->thumb_resident_bytes += bytes;
	if (app->thumb_resident_bytes > app->thumb_resident_peak_bytes) {
		app->thumb_resident_peak_bytes = app->thumb_resident_bytes;
	}
	if (result->cache_hit) {
		app->thumb_cache_hits++;
	} else {
		app->thumb_decoded++;
	}
}
