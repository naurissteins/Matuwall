#ifndef SWEETWALL_APP_PREVIEW_H
#define SWEETWALL_APP_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thumb/decode.h"
#include "thumb/worker.h"

struct sweetwall_app;

// Output-sized backdrop for the current selection. Only one image is held at
// a time; it is the largest allocation in the process
struct sweetwall_preview {
	struct sweetwall_image image;
	size_t shown;
	size_t wanted;
	size_t in_flight;
	// Monotonic ms deadline for the dwell, or 0 when nothing is waiting
	int64_t due_ms;
	uint32_t target_w;
	uint32_t target_h;
	bool enabled;
};

void sweetwall_app_preview_init(struct sweetwall_app *app);

void sweetwall_app_preview_select(
	struct sweetwall_app *app, size_t index, int64_t now_ms);

int sweetwall_app_preview_timeout(
	const struct sweetwall_app *app, int64_t now_ms);

void sweetwall_app_preview_tick(struct sweetwall_app *app, int64_t now_ms);

void sweetwall_app_preview_result(
	struct sweetwall_app *app, const struct sweetwall_thumb_result *result);

void sweetwall_app_preview_finish(struct sweetwall_app *app);

#endif
