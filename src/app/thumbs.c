#include "app/thumbs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "app/app.h"
#include "app/preview.h"
#include "app/thumb_store.h"
#include "thumb/worker.h"
#include "util/log.h"

static void on_result(
	void *user_data, const struct matuwall_thumb_result *result) {
	struct matuwall_app *app = user_data;
	if (result->kind == MATUWALL_JOB_PREVIEW) {
		// cancelled decode is superseded, not broken
		if (!result->ok && !result->cancelled &&
			result->index < app->scan.count) {
			matuwall_log_warn("preview", "could not decode %s",
				app->scan.paths[result->index]);
		}
		matuwall_app_preview_result(app, result);
		return;
	}
	if (result->index >= app->thumbs.count) {
		free(result->image.pixels);
		return;
	}

	struct matuwall_thumb *thumb = &app->thumbs.items[result->index];
	if (thumb->state != MATUWALL_THUMB_PENDING) {
		free(result->image.pixels);
		return;
	}

	if (result->ok) {
		matuwall_thumb_store_accept(app, result);
	} else {
		thumb->state = MATUWALL_THUMB_FAILED;
		matuwall_log_warn("thumbnail", "could not decode %s",
			app->scan.paths[result->index]);
	}
	// lookahead lands off screen, and the next visit repaints anyway
	if (matuwall_thumb_store_drawable(app, result->index)) {
		app->layer.needs_repaint = true;
	}
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

static size_t pending_in_range(
	const struct matuwall_app *app, size_t first, size_t end) {
	if (end > app->thumbs.count) {
		end = app->thumbs.count;
	}
	size_t pending = 0;
	for (size_t i = first; i < end; i++) {
		pending += app->thumbs.items[i].state == MATUWALL_THUMB_PENDING;
	}
	return pending;
}

static void refresh_visible_pending(struct matuwall_app *app) {
	if (app->thumbs.items == NULL) {
		app->thumbs.visible_pending = 0;
		return;
	}
	size_t first;
	size_t end;
	size_t wrap_end;
	matuwall_thumb_store_visible_ranges(app, &first, &end, &wrap_end);
	app->thumbs.visible_pending = pending_in_range(app, first, end) +
				      pending_in_range(app, 0, wrap_end);
}

static void submit_index(struct matuwall_app *app, size_t index) {
	if (index >= app->thumbs.count ||
		app->thumbs.items[index].state != MATUWALL_THUMB_UNLOADED) {
		return;
	}
	if (matuwall_worker_submit(
		    app->workers, index, app->scan.paths[index])) {
		app->thumbs.items[index].state = MATUWALL_THUMB_PENDING;
		return;
	}
	app->thumbs.items[index].state = MATUWALL_THUMB_FAILED;
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

	app->thumbs.items = calloc(app->scan.count, sizeof(*app->thumbs.items));
	if (app->thumbs.items == NULL) {
		matuwall_log_error(
			"thumbnail", "cannot allocate thumbnail state");
		return;
	}
	app->thumbs.count = app->scan.count;

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

	matuwall_app_thumbs_prioritize_visible(app);
}

struct thumb_window {
	struct matuwall_app *app;
	size_t first;
	size_t end;
	size_t wrap_end;
};

// runs under the pool lock, pure reads of main-thread state
static bool keep_queued(void *user_data, size_t index) {
	const struct thumb_window *window = user_data;
	return matuwall_thumb_store_in_window(window->app, index, window->first,
		window->end, window->wrap_end);
}

// withdrawn job never runs, so the tile can be requested again
static void withdraw_queued(void *user_data, size_t index) {
	struct thumb_window *window = user_data;
	struct matuwall_app *app = window->app;
	if (index >= app->thumbs.count ||
		app->thumbs.items[index].state != MATUWALL_THUMB_PENDING) {
		return;
	}
	app->thumbs.items[index].state = MATUWALL_THUMB_UNLOADED;
	app->thumbs.withdrawn++;
}

void matuwall_app_thumbs_prioritize_visible(struct matuwall_app *app) {
	if (app->workers == NULL) {
		app->thumbs.visible_pending = 0;
		return;
	}

	size_t first;
	size_t end;
	size_t wrap_end;
	matuwall_thumb_store_visible_ranges(app, &first, &end, &wrap_end);
	matuwall_thumb_store_evict_outside(app, first, end, wrap_end);
	submit_visible_window(app, first, end, wrap_end);
	refresh_visible_pending(app);
	if (app->thumbs.priority_set && first == app->thumbs.priority_first &&
		end == app->thumbs.priority_end &&
		wrap_end == app->thumbs.priority_wrap_end) {
		return;
	}

	// old viewports are withdrawn, not just reordered behind the new one
	struct thumb_window window = {
		.app = app,
		.first = first,
		.end = end,
		.wrap_end = wrap_end,
	};
	const struct matuwall_thumb_filter filter = {
		.keep = keep_queued,
		.dropped = withdraw_queued,
		.user_data = &window,
	};
	matuwall_worker_prioritize_thumbs(
		app->workers, first, end, wrap_end, &filter);
	app->thumbs.priority_first = first;
	app->thumbs.priority_end = end;
	app->thumbs.priority_wrap_end = wrap_end;
	app->thumbs.priority_set = true;
}

void matuwall_app_thumbs_drain(struct matuwall_app *app) {
	if (app->workers != NULL) {
		matuwall_worker_drain(app->workers, on_result, app);
		refresh_visible_pending(app);
	}
}

static void release_thumbs(struct matuwall_app *app) {
	if (app->thumbs.count > 0) {
		size_t states[MATUWALL_THUMB_FAILED + 1] = {0};
		for (size_t i = 0; i < app->thumbs.count; i++) {
			states[app->thumbs.items[i].state]++;
		}
		matuwall_log_info("thumbnail",
			"summary: %zu cache hit%s, %zu decoded, %zu failed, "
			"%zu discarded, %zu withdrawn, %zu unrequested, "
			"%zu unfinished, %zu evicted, %zu KiB peak resident",
			app->thumbs.cache_hits,
			app->thumbs.cache_hits == 1 ? "" : "s",
			app->thumbs.decoded, states[MATUWALL_THUMB_FAILED],
			app->thumbs.discarded, app->thumbs.withdrawn,
			states[MATUWALL_THUMB_UNLOADED],
			states[MATUWALL_THUMB_PENDING], app->thumbs.evicted,
			app->thumbs.resident_peak_bytes / 1024);
	}
	if (app->thumbs.items != NULL) {
		for (size_t i = 0; i < app->thumbs.count; i++) {
			free(app->thumbs.items[i].pixels);
		}
		free(app->thumbs.items);
	}
	app->thumbs = (struct matuwall_thumb_store){0};
}

void matuwall_app_thumbs_quiesce(struct matuwall_app *app) {
	if (app->workers != NULL) {
		matuwall_worker_pool_request_stop(app->workers);
	}
	release_thumbs(app);
}

void matuwall_app_thumbs_finish(struct matuwall_app *app) {
	if (app->workers != NULL) {
		matuwall_worker_pool_stop(app->workers);
		app->workers = NULL;
	}
	release_thumbs(app);
}
