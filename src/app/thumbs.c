#include "app/thumbs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "app/app.h"
#include "app/preview.h"
#include "thumb/worker.h"

static void on_result(
	void *user_data, const struct sweetwall_thumb_result *result) {
	struct sweetwall_app *app = user_data;
	if (result->kind == SWEETWALL_JOB_PREVIEW) {
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
	} else {
		thumb->state = SWEETWALL_THUMB_FAILED;
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
	*tw = (uint32_t)(app->config.layout.tile_width * scale + 0.5);
	*th = (uint32_t)(app->config.layout.tile_height * scale + 0.5);
}

void sweetwall_app_thumbs_start(struct sweetwall_app *app) {
	if (app->scan.count == 0) {
		return;
	}

	app->thumbs = calloc(app->scan.count, sizeof(*app->thumbs));
	if (app->thumbs == NULL) {
		return;
	}
	app->thumb_count = app->scan.count;

	uint32_t tw;
	uint32_t th;
	thumbnail_target(app, &tw, &th);
	app->workers = sweetwall_worker_pool_start(tw, th);
	if (app->workers == NULL) {
		fprintf(stderr, "sweetwall: failed to start thumbnail "
				"workers\n");
		return;
	}

	for (size_t i = 0; i < app->scan.count; i++) {
		if (sweetwall_worker_submit(
			    app->workers, i, app->scan.paths[i])) {
			app->pending++;
		} else {
			app->thumbs[i].state = SWEETWALL_THUMB_FAILED;
		}
	}
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
	if (app->thumbs != NULL) {
		for (size_t i = 0; i < app->thumb_count; i++) {
			free(app->thumbs[i].pixels);
		}
		free(app->thumbs);
		app->thumbs = NULL;
		app->thumb_count = 0;
	}
}
