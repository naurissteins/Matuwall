#include "app/preview.h"

#include <stdlib.h>

#include "app/app.h"
#include "render/image.h"
#include "util/log.h"

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
	app->preview = (struct matuwall_preview){
		.shown = SIZE_MAX,
		.wanted = SIZE_MAX,
		.in_flight = SIZE_MAX,
		.enabled =
			app->config.preview && app->backdrop.wl_surface != NULL,
	};
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
	// an unconfigured backdrop has no target size; its configure wakes us
	if (!preview->enabled || preview->due_ms == 0 ||
		!app->backdrop.configured) {
		return -1;
	}
	int64_t left = preview->due_ms - now_ms;
	return left > 0 ? (int)left : 0;
}

// bound retained backdrop pixels while preserving the output aspect
static bool preview_target(
	struct matuwall_app *app, uint32_t *width, uint32_t *height) {
	matuwall_layer_inherit_scale(&app->backdrop, &app->layer);
	matuwall_layer_buffer_size(&app->backdrop, width, height);
	if (*width == 0 || *height == 0) {
		return false;
	}
	cap_preview_target(width, height);
	return true;
}

void matuwall_app_preview_tick(struct matuwall_app *app, int64_t now_ms) {
	struct matuwall_preview *preview = &app->preview;
	if (!preview->enabled || preview->due_ms == 0 ||
		now_ms < preview->due_ms || !app->backdrop.configured) {
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
	uint32_t width;
	uint32_t height;
	if (!preview_target(app, &width, &height)) {
		return;
	}
	// Supersedes any running decode; only a job allocation can fail
	if (!matuwall_worker_submit_preview(app->workers, preview->wanted,
		    app->scan.paths[preview->wanted], width, height)) {
		preview->due_ms = now_ms + PREVIEW_DWELL_MS;
		return;
	}
	preview->in_flight = preview->wanted;
}

void matuwall_app_preview_result(
	struct matuwall_app *app, const struct matuwall_thumb_result *result) {
	struct matuwall_preview *preview = &app->preview;
	// a decode that outlived preview_disable has nowhere to go
	if (result->cancelled || !preview->enabled) {
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
	preview->shown = result->index;
	app->backdrop.needs_repaint = true;
}

// the picker still works without its backdrop, so a failure only drops it
static void preview_disable(struct matuwall_app *app, const char *reason) {
	struct matuwall_preview *preview = &app->preview;
	matuwall_log_warn("preview", "%s, preview disabled", reason);
	if (preview->in_flight != SIZE_MAX && app->workers != NULL) {
		matuwall_worker_cancel_preview(app->workers);
	}
	preview->in_flight = SIZE_MAX;
	preview->due_ms = 0;
	matuwall_image_free(&preview->image);
	preview->enabled = false;
	// nothing paints the backdrop again, held buffers wait for teardown
	matuwall_buffer_pool_collect_idle(&app->backdrop.buffer_pool, 0, 0);
}

void matuwall_app_preview_render(struct matuwall_app *app, int64_t now_ms) {
	struct matuwall_preview *preview = &app->preview;
	struct matuwall_layer *layer = &app->backdrop;
	if (!preview->enabled) {
		return;
	}
	if (layer->closed) {
		preview_disable(app, "compositor closed the backdrop surface");
		return;
	}
	if (!layer->needs_repaint || !layer->configured) {
		return;
	}
	layer->layout_dirty = false;
	if (preview->image.pixels == NULL) {
		// resized or rescaled after the image was freed, decode it
		// again
		if (preview->shown != SIZE_MAX) {
			preview->shown = SIZE_MAX;
			preview->due_ms = now_ms;
		}
		layer->needs_repaint = false;
		return;
	}

	struct matuwall_buffer *buffer;
	enum matuwall_buffer_acquire acquired =
		matuwall_layer_begin_frame(layer, app->registry.shm, &buffer);
	if (acquired == MATUWALL_BUFFER_BUSY) {
		return;
	}
	if (acquired == MATUWALL_BUFFER_FAILED) {
		preview_disable(app, "cannot allocate a backdrop buffer");
		return;
	}
	struct matuwall_clip whole = {
		.x1 = (int32_t)buffer->width,
		.y1 = (int32_t)buffer->height,
	};
	matuwall_draw_image_cover_clipped(buffer, &whole, preview->image.pixels,
		preview->image.width, preview->image.height);
	struct matuwall_damage damage = {
		.x1 = (int32_t)buffer->width,
		.y1 = (int32_t)buffer->height,
	};
	if (!matuwall_layer_commit_frame(layer, false, true, &damage)) {
		preview_disable(app, "cannot commit the backdrop");
		return;
	}
	// the surface keeps showing it, only a resize needs the pixels again
	matuwall_image_free(&preview->image);
}

void matuwall_app_preview_finish(struct matuwall_app *app) {
	matuwall_image_free(&app->preview.image);
	app->preview.shown = SIZE_MAX;
}
