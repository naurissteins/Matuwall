#include "app/preview.h"

#include <stdlib.h>

#include "app/app.h"

// Settle time before a decode is worth starting; holding an arrow key must not
// queue one full-resolution decode per step
#define PREVIEW_DWELL_MS 60

void matuwall_app_preview_init(struct matuwall_app *app) {
	struct matuwall_preview *preview = &app->preview;
	*preview = (struct matuwall_preview){
		.shown = SIZE_MAX,
		.wanted = SIZE_MAX,
		.in_flight = SIZE_MAX,
		.enabled = app->config.preview,
	};
	if (!preview->enabled) {
		return;
	}

	// Physical pixels: the backdrop covers the buffer one to one
	matuwall_layer_buffer_size(
		&app->layer, &preview->target_w, &preview->target_h);
	if (preview->target_w == 0 || preview->target_h == 0) {
		preview->enabled = false;
	}
}

void matuwall_app_preview_select(
	struct matuwall_app *app, size_t index, int64_t now_ms) {
	struct matuwall_preview *preview = &app->preview;
	if (!preview->enabled || index >= app->scan.count ||
		index == preview->wanted) {
		return;
	}
	preview->wanted = index;
	preview->due_ms = now_ms + PREVIEW_DWELL_MS;
}

int matuwall_app_preview_timeout(
	const struct matuwall_app *app, int64_t now_ms) {
	const struct matuwall_preview *preview = &app->preview;
	if (!preview->enabled || preview->due_ms == 0) {
		return -1;
	}
	int64_t left = preview->due_ms - now_ms;
	return left > 0 ? (int)left : 0;
}

void matuwall_app_preview_tick(struct matuwall_app *app, int64_t now_ms) {
	struct matuwall_preview *preview = &app->preview;
	if (!preview->enabled || preview->due_ms == 0 ||
		now_ms < preview->due_ms) {
		return;
	}
	preview->due_ms = 0;

	if (app->workers == NULL || preview->wanted == preview->shown ||
		preview->wanted == preview->in_flight) {
		return;
	}
	if (matuwall_worker_submit_preview(app->workers, preview->wanted,
		    app->scan.paths[preview->wanted], preview->target_w,
		    preview->target_h)) {
		preview->in_flight = preview->wanted;
	}
}

void matuwall_app_preview_result(
	struct matuwall_app *app, const struct matuwall_thumb_result *result) {
	struct matuwall_preview *preview = &app->preview;
	if (result->index == preview->in_flight) {
		preview->in_flight = SIZE_MAX;
	}

	// A superseded decode landed late; the selection has moved on
	if (!result->ok || result->index != preview->wanted) {
		free(result->pixels);
		return;
	}

	free(preview->image.pixels);
	preview->image = (struct matuwall_image){
		.width = result->width,
		.height = result->height,
		.pixels = result->pixels,
	};
	preview->generation++;
	preview->shown = result->index;
	app->layer.needs_repaint = true;
}

void matuwall_app_preview_finish(struct matuwall_app *app) {
	matuwall_image_free(&app->preview.image);
	app->preview.shown = SIZE_MAX;
}
