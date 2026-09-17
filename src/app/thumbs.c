#include "app/thumbs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "app/app.h"
#include "app/preview.h"
#include "app/thumb_store.h"
#include "thumb/worker.h"
#include "util/log.h"

static void accept_thumbnail_result(
	struct matuwall_app *app, const struct matuwall_thumb_result *result);

static void on_result(
	void *user_data, const struct matuwall_thumb_result *result) {
	struct matuwall_app *app = user_data;
	if (result->kind == MATUWALL_JOB_PREVIEW) {
		if (!result->ok && result->index < app->scan.count) {
			matuwall_log_warn("preview", "could not decode %s",
				app->scan.paths[result->index]);
		}
		matuwall_app_preview_result(app, result);
		return;
	}
	if (result->index >= app->thumb_count) {
		free(result->pixels);
		return;
	}

	struct matuwall_thumb *thumb = &app->thumbs[result->index];
	if (thumb->state != MATUWALL_THUMB_PENDING) {
		free(result->pixels);
		return;
	}

	if (result->ok) {
		accept_thumbnail_result(app, result);
	} else {
		thumb->state = MATUWALL_THUMB_FAILED;
		app->thumb_failed++;
		matuwall_log_warn("thumbnail", "could not decode %s",
			app->scan.paths[result->index]);
	}
	if (app->pending > 0) {
		app->pending--;
	}
	app->layer.needs_repaint = true;
}

// Fit the physical target inside the output without changing its aspect
static bool thumbnail_target(
	const struct matuwall_app *app, uint32_t *tw, uint32_t *th) {
	uint32_t pw;
	uint32_t ph;
	matuwall_layer_buffer_size(&app->layer, &pw, &ph);
	if (pw == 0 || ph == 0) {
		return false;
	}

	double scale =
		app->layer.width > 0 ? (double)pw / app->layer.width : 1.0;
	double width = app->layout.tile_width * scale;
	double height = app->layout.tile_height * scale;
	double fit = 1.0;
	if (width > pw) {
		fit = (double)pw / width;
	}
	if (height * fit > ph) {
		fit = (double)ph / height;
	}

	width *= fit;
	height *= fit;
	*tw = width >= 1.0 ? (uint32_t)(width + 0.5) : 1;
	*th = height >= 1.0 ? (uint32_t)(height + 0.5) : 1;
	if (*tw > pw) {
		*tw = pw;
	}
	if (*th > ph) {
		*th = ph;
	}
	return true;
}

static void visible_ranges(const struct matuwall_app *app, size_t *first,
	size_t *end, size_t *wrap_end) {
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

static void accept_thumbnail_result(
	struct matuwall_app *app, const struct matuwall_thumb_result *result) {
	size_t first;
	size_t end;
	size_t wrap_end;
	visible_ranges(app, &first, &end, &wrap_end);
	matuwall_thumb_store_accept(app, result, first, end, wrap_end);
}

static size_t pending_in_range(
	const struct matuwall_app *app, size_t first, size_t end) {
	if (end > app->thumb_count) {
		end = app->thumb_count;
	}
	size_t pending = 0;
	for (size_t i = first; i < end; i++) {
		pending += app->thumbs[i].state == MATUWALL_THUMB_PENDING;
	}
	return pending;
}

static void refresh_visible_pending(
	struct matuwall_app *app, size_t first, size_t end, size_t wrap_end) {
	if (app->thumbs == NULL) {
		app->visible_pending = 0;
		return;
	}
	app->visible_pending = pending_in_range(app, first, end) +
			       pending_in_range(app, 0, wrap_end);
}

static void refresh_current_visible_pending(struct matuwall_app *app) {
	size_t first;
	size_t end;
	size_t wrap_end;
	visible_ranges(app, &first, &end, &wrap_end);
	refresh_visible_pending(app, first, end, wrap_end);
}

static void submit_index(struct matuwall_app *app, size_t index) {
	if (index >= app->thumb_count ||
		app->thumbs[index].state != MATUWALL_THUMB_UNLOADED) {
		return;
	}
	if (matuwall_worker_submit(
		    app->workers, index, app->scan.paths[index])) {
		app->thumbs[index].state = MATUWALL_THUMB_PENDING;
		app->pending++;
		return;
	}
	app->thumbs[index].state = MATUWALL_THUMB_FAILED;
	app->thumb_failed++;
	matuwall_log_warn(
		"thumbnail", "could not queue %s", app->scan.paths[index]);
}

static void submit_range(struct matuwall_app *app, size_t first, size_t end) {
	for (size_t i = first; i < end; i++) {
		submit_index(app, i);
	}
}

static void submit_grid_lookahead(
	struct matuwall_app *app, size_t first, size_t end, size_t count) {
	for (size_t offset = 0; offset < count; offset++) {
		if (end + offset < app->scan.count) {
			submit_index(app, end + offset);
		}
		if (offset < first) {
			submit_index(app, first - offset - 1);
		}
	}
}

static void submit_carousel_lookahead(struct matuwall_app *app, size_t first,
	size_t end, size_t wrap_end, size_t count) {
	size_t after = wrap_end > 0 || end == app->scan.count ? wrap_end : end;
	size_t before = first == 0 ? app->scan.count - 1 : first - 1;
	for (size_t offset = 0; offset < count; offset++) {
		submit_index(app, after);
		submit_index(app, before);
		after = after + 1 == app->scan.count ? 0 : after + 1;
		before = before == 0 ? app->scan.count - 1 : before - 1;
	}
}

static void submit_visible_window(
	struct matuwall_app *app, size_t first, size_t end, size_t wrap_end) {
	submit_range(app, first, end);
	submit_range(app, 0, wrap_end);
	size_t visible = end - first + wrap_end;
	if (visible >= app->scan.count) {
		return;
	}
	// One viewport each way keeps the next navigation step warm
	size_t lookahead = matuwall_thumb_store_lookahead(app, visible);
	if (app->layout.flow == MATUWALL_FLOW_GRID) {
		submit_grid_lookahead(app, first, end, lookahead);
	} else {
		submit_carousel_lookahead(app, first, end, wrap_end, lookahead);
	}
}

void matuwall_app_thumbs_start(struct matuwall_app *app) {
	if (app->scan.count == 0) {
		return;
	}

	app->thumbs = calloc(app->scan.count, sizeof(*app->thumbs));
	if (app->thumbs == NULL) {
		matuwall_log_error(
			"thumbnail", "cannot allocate thumbnail state");
		return;
	}
	app->thumb_count = app->scan.count;

	uint32_t tw;
	uint32_t th;
	if (!thumbnail_target(app, &tw, &th) ||
		!matuwall_thumb_store_set_target(app, tw, th)) {
		matuwall_log_error(
			"thumbnail", "invalid thumbnail target size");
		return;
	}
	app->workers = matuwall_worker_pool_start(tw, th);
	if (app->workers == NULL) {
		matuwall_log_error("thumbnail", "failed to start workers");
		return;
	}

	size_t first;
	size_t end;
	size_t wrap_end;
	visible_ranges(app, &first, &end, &wrap_end);
	matuwall_thumb_store_evict_outside(app, first, end, wrap_end);
	submit_visible_window(app, first, end, wrap_end);
	app->thumb_priority_first = first;
	app->thumb_priority_end = end;
	app->thumb_priority_wrap_end = wrap_end;
	app->thumb_priority_set = true;
	refresh_visible_pending(app, first, end, wrap_end);
}

void matuwall_app_thumbs_prioritize_visible(struct matuwall_app *app) {
	if (app->workers == NULL) {
		app->visible_pending = 0;
		return;
	}

	size_t first;
	size_t end;
	size_t wrap_end;
	visible_ranges(app, &first, &end, &wrap_end);
	matuwall_thumb_store_evict_outside(app, first, end, wrap_end);
	submit_visible_window(app, first, end, wrap_end);
	refresh_visible_pending(app, first, end, wrap_end);
	if (app->thumb_priority_set && first == app->thumb_priority_first &&
		end == app->thumb_priority_end &&
		wrap_end == app->thumb_priority_wrap_end) {
		return;
	}

	matuwall_worker_prioritize_thumbs(app->workers, first, end, wrap_end);
	app->thumb_priority_first = first;
	app->thumb_priority_end = end;
	app->thumb_priority_wrap_end = wrap_end;
	app->thumb_priority_set = true;
}

void matuwall_app_thumbs_drain(struct matuwall_app *app) {
	if (app->workers != NULL) {
		matuwall_worker_drain(app->workers, on_result, app);
		refresh_current_visible_pending(app);
	}
}

void matuwall_app_thumbs_finish(struct matuwall_app *app) {
	if (app->workers != NULL) {
		matuwall_worker_pool_stop(app->workers);
		app->workers = NULL;
	}
	if (app->thumb_count > 0) {
		size_t unrequested = 0;
		for (size_t i = 0; i < app->thumb_count; i++) {
			unrequested +=
				app->thumbs[i].state == MATUWALL_THUMB_UNLOADED;
		}
		size_t unfinished = app->pending;
		matuwall_log_info("thumbnail",
			"summary: %zu cache hit%s, %zu decoded, %zu failed, "
			"%zu unrequested, %zu unfinished, %zu evicted, "
			"%zu KiB peak resident",
			app->thumb_cache_hits,
			app->thumb_cache_hits == 1 ? "" : "s",
			app->thumb_decoded, app->thumb_failed, unrequested,
			unfinished, app->thumb_evicted,
			app->thumb_resident_peak_bytes / 1024);
	}
	if (app->thumbs != NULL) {
		for (size_t i = 0; i < app->thumb_count; i++) {
			free(app->thumbs[i].pixels);
		}
		free(app->thumbs);
		app->thumbs = NULL;
		app->thumb_count = 0;
	}
	app->thumb_priority_first = 0;
	app->thumb_priority_end = 0;
	app->thumb_priority_set = false;
	app->pending = 0;
	app->visible_pending = 0;
	app->thumb_target_bytes = 0;
	app->thumb_resident_bytes = 0;
	app->thumb_resident_peak_bytes = 0;
	app->thumb_evicted = 0;
	app->thumb_cache_hits = 0;
	app->thumb_decoded = 0;
	app->thumb_failed = 0;
}
