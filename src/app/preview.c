#include "app/preview.h"

#include <stdlib.h>

#include "app/app.h"

#define PREVIEW_LONG_EDGE_MAX 4096u
// Settle time before a decode is worth starting; holding an arrow key must not
// queue one full-resolution decode per step
#define PREVIEW_DWELL_MS 60

static void cap_preview_target(uint32_t *width, uint32_t *height) {
	if (*width <= PREVIEW_LONG_EDGE_MAX &&
		*height <= PREVIEW_LONG_EDGE_MAX) {
		return;
	}

	if (*width >= *height) {
		uint32_t scaled =
			(uint32_t)(((uint64_t)*height * PREVIEW_LONG_EDGE_MAX +
					   *width / 2) /
				   *width);
		*width = PREVIEW_LONG_EDGE_MAX;
		*height = scaled > 0 ? scaled : 1;
	} else {
		uint32_t scaled =
			(uint32_t)(((uint64_t)*width * PREVIEW_LONG_EDGE_MAX +
					   *height / 2) /
				   *height);
		*width = scaled > 0 ? scaled : 1;
		*height = PREVIEW_LONG_EDGE_MAX;
	}
}

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

	// Bound retained backdrop pixels while preserving the output aspect
	matuwall_layer_buffer_size(
		&app->layer, &preview->target_w, &preview->target_h);
	if (preview->target_w == 0 || preview->target_h == 0) {
		preview->enabled = false;
		return;
	}
	cap_preview_target(&preview->target_w, &preview->target_h);
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

	if (app->workers == NULL || preview->wanted == preview->in_flight) {
		return;
	}
	// Back on the shown backdrop, so a pending decode is stale work
	if (preview->wanted == preview->shown) {
		if (preview->in_flight != SIZE_MAX) {
			matuwall_worker_cancel_preview(app->workers);
			preview->in_flight = SIZE_MAX;
		}
		return;
	}
	// Supersedes any running decode; only a job allocation can fail
	if (!matuwall_worker_submit_preview(app->workers, preview->wanted,
		    app->scan.paths[preview->wanted], preview->target_w,
		    preview->target_h)) {
		preview->due_ms = now_ms + PREVIEW_DWELL_MS;
		return;
	}
	preview->in_flight = preview->wanted;
}

void matuwall_app_preview_result(
	struct matuwall_app *app, const struct matuwall_thumb_result *result) {
	struct matuwall_preview *preview = &app->preview;
	if (result->cancelled) {
		free(result->pixels);
		return;
	}
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
