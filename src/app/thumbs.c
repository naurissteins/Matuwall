#include "app/thumbs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "app/app.h"
#include "app/preview.h"
#include "thumb/worker.h"
#include "util/log.h"

static void on_result(
	void *user_data, const struct sweetwall_thumb_result *result) {
	struct sweetwall_app *app = user_data;
	if (result->kind == SWEETWALL_JOB_PREVIEW) {
		if (!result->ok && result->index < app->scan.count) {
			sweetwall_log_warn("preview", "could not decode %s",
				app->scan.paths[result->index]);
		}
		sweetwall_app_preview_result(app, result);
		return;
	}
	if (result->index >= app->thumb_count) {
		free(result->pixels);
		return;
	}

	struct sweetwall_thumb *thumb = &app->thumbs[result->index];
	if (thumb->state != SWEETWALL_THUMB_PENDING) {
		free(result->pixels);
		return;
	}

	if (result->ok) {
		thumb->state = SWEETWALL_THUMB_READY;
		thumb->pixels = result->pixels;
		thumb->width = result->width;
		thumb->height = result->height;
		if (result->cache_hit) {
			app->thumb_cache_hits++;
		} else {
			app->thumb_decoded++;
		}
	} else {
		thumb->state = SWEETWALL_THUMB_FAILED;
		app->thumb_failed++;
		sweetwall_log_warn("thumbnail", "could not decode %s",
			app->scan.paths[result->index]);
	}
	if (app->pending > 0) {
		app->pending--;
	}
	app->layer.needs_repaint = true;
}

// Thumbnail target size in physical pixels, scaled to the current output
static void thumbnail_target(
	const struct sweetwall_app *app, uint32_t *tw, uint32_t *th) {
	uint32_t pw;
	uint32_t ph;
	sweetwall_layer_buffer_size(&app->layer, &pw, &ph);
	double scale =
		app->layer.width > 0 ? (double)pw / app->layer.width : 1.0;
	*tw = (uint32_t)(app->layout.tile_width * scale + 0.5);
	*th = (uint32_t)(app->layout.tile_height * scale + 0.5);
}

static void visible_range(
	const struct sweetwall_app *app, size_t *first, size_t *end) {
	size_t columns = app->layout.columns;
	if (columns == 0) {
		columns = 1;
	}

	*first = (size_t)app->grid.first_row * columns;
	if (*first >= app->scan.count) {
		*first = app->scan.count;
		*end = app->scan.count;
		return;
	}

	uint32_t height =
		app->panel.height > 0 ? (uint32_t)app->panel.height : 0;
	size_t rows = sweetwall_grid_visible_rows(&app->layout, height);
	size_t count = rows * columns;
	size_t remaining = app->scan.count - *first;
	*end = *first + (count < remaining ? count : remaining);
}

static void submit_range(struct sweetwall_app *app, size_t first, size_t end) {
	for (size_t i = first; i < end; i++) {
		if (sweetwall_worker_submit(
			    app->workers, i, app->scan.paths[i])) {
			app->pending++;
		} else {
			app->thumbs[i].state = SWEETWALL_THUMB_FAILED;
			app->thumb_failed++;
			sweetwall_log_warn("thumbnail", "could not queue %s",
				app->scan.paths[i]);
		}
	}
}

void sweetwall_app_thumbs_start(struct sweetwall_app *app) {
	if (app->scan.count == 0) {
		return;
	}

	app->thumbs = calloc(app->scan.count, sizeof(*app->thumbs));
	if (app->thumbs == NULL) {
		sweetwall_log_error(
			"thumbnail", "cannot allocate thumbnail state");
		return;
	}
	app->thumb_count = app->scan.count;

	uint32_t tw;
	uint32_t th;
	thumbnail_target(app, &tw, &th);
	app->workers = sweetwall_worker_pool_start(tw, th);
	if (app->workers == NULL) {
		sweetwall_log_error("thumbnail", "failed to start workers");
		return;
	}

	size_t first;
	size_t end;
	visible_range(app, &first, &end);
	submit_range(app, first, end);
	submit_range(app, 0, first);
	submit_range(app, end, app->scan.count);
	app->thumb_priority_first = first;
	app->thumb_priority_end = end;
	app->thumb_priority_set = true;
}

void sweetwall_app_thumbs_prioritize_visible(struct sweetwall_app *app) {
	if (app->workers == NULL) {
		return;
	}

	size_t first;
	size_t end;
	visible_range(app, &first, &end);
	if (app->thumb_priority_set && first == app->thumb_priority_first &&
		end == app->thumb_priority_end) {
		return;
	}

	sweetwall_worker_prioritize_thumbs(app->workers, first, end);
	app->thumb_priority_first = first;
	app->thumb_priority_end = end;
	app->thumb_priority_set = true;
}

void sweetwall_app_thumbs_drain(struct sweetwall_app *app) {
	if (app->workers != NULL) {
		sweetwall_worker_drain(app->workers, on_result, app);
	}
}

void sweetwall_app_thumbs_finish(struct sweetwall_app *app) {
	if (app->workers != NULL) {
		sweetwall_worker_pool_stop(app->workers);
		app->workers = NULL;
	}
	if (app->thumb_count > 0) {
		size_t pending = app->thumb_count - app->thumb_cache_hits -
				 app->thumb_decoded - app->thumb_failed;
		sweetwall_log_info("thumbnail",
			"summary: %zu cache hit%s, %zu decoded, %zu failed, "
			"%zu unfinished",
			app->thumb_cache_hits,
			app->thumb_cache_hits == 1 ? "" : "s",
			app->thumb_decoded, app->thumb_failed, pending);
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
	app->thumb_cache_hits = 0;
	app->thumb_decoded = 0;
	app->thumb_failed = 0;
}
