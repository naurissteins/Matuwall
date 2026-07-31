#include "app_preview.h"

#include <stdlib.h>

#include "app.h"

// Settle time before a decode is worth starting; holding an arrow key must not
// queue one full-resolution decode per step
#define PREVIEW_DWELL_MS 60

void sweetwall_app_preview_init(struct sweetwall_app *app) {
	struct sweetwall_preview *preview = &app->preview;
	*preview = (struct sweetwall_preview){
		.shown = SIZE_MAX,
		.wanted = SIZE_MAX,
		.in_flight = SIZE_MAX,
		.enabled = app->config.preview,
	};
	if (!preview->enabled) {
		return;
	}

	// Physical pixels: the backdrop covers the buffer one to one
	sweetwall_layer_buffer_size(
		&app->layer, &preview->target_w, &preview->target_h);
	if (preview->target_w == 0 || preview->target_h == 0) {
		preview->enabled = false;
	}
}

void sweetwall_app_preview_select(
	struct sweetwall_app *app, size_t index, int64_t now_ms) {
	struct sweetwall_preview *preview = &app->preview;
	if (!preview->enabled || index >= app->scan.count ||
		index == preview->wanted) {
		return;
	}
	preview->wanted = index;
	preview->due_ms = now_ms + PREVIEW_DWELL_MS;
}

int sweetwall_app_preview_timeout(
	const struct sweetwall_app *app, int64_t now_ms) {
	const struct sweetwall_preview *preview = &app->preview;
	if (!preview->enabled || preview->due_ms == 0) {
		return -1;
	}
	int64_t left = preview->due_ms - now_ms;
	return left > 0 ? (int)left : 0;
}

void sweetwall_app_preview_tick(struct sweetwall_app *app, int64_t now_ms) {
	struct sweetwall_preview *preview = &app->preview;
	if (!preview->enabled || preview->due_ms == 0 ||
		now_ms < preview->due_ms) {
		return;
	}
	preview->due_ms = 0;

	if (app->workers == NULL || preview->wanted == preview->shown ||
		preview->wanted == preview->in_flight) {
		return;
	}
	if (sweetwall_worker_submit_preview(app->workers, preview->wanted,
		    app->scan.paths[preview->wanted], preview->target_w,
		    preview->target_h)) {
		preview->in_flight = preview->wanted;
	}
}

void sweetwall_app_preview_result(struct sweetwall_app *app,
	const struct sweetwall_thumb_result *result) {
	struct sweetwall_preview *preview = &app->preview;
	if (result->index == preview->in_flight) {
		preview->in_flight = SIZE_MAX;
	}

	// A superseded decode landed late; the selection has moved on
	if (!result->ok || result->index != preview->wanted) {
		free(result->pixels);
		return;
	}

	free(preview->image.pixels);
	preview->image = (struct sweetwall_image){
		.width = result->width,
		.height = result->height,
		.pixels = result->pixels,
	};
	preview->shown = result->index;
	app->layer.needs_repaint = true;
}

void sweetwall_app_preview_finish(struct sweetwall_app *app) {
	sweetwall_image_free(&app->preview.image);
	app->preview.shown = SIZE_MAX;
}
