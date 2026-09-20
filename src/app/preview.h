#ifndef MATUWALL_APP_PREVIEW_H
#define MATUWALL_APP_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "thumb/decode.h"
#include "thumb/worker.h"

struct matuwall_app;

// Capped backdrop with one displayed image and one outstanding decode
struct matuwall_preview {
	struct matuwall_image image;
	size_t shown;
	size_t wanted;
	// One submitted preview until its result is drained
	size_t in_flight;
	// Dwell deadline stays pending while an older preview runs
	int64_t due_ms;
	uint32_t target_w;
	uint32_t target_h;
	uint64_t generation;
	bool enabled;
};

void matuwall_app_preview_init(struct matuwall_app *app);

void matuwall_app_preview_select(
	struct matuwall_app *app, size_t index, int64_t now_ms);

int matuwall_app_preview_timeout(
	const struct matuwall_app *app, int64_t now_ms);

void matuwall_app_preview_tick(struct matuwall_app *app, int64_t now_ms);

void matuwall_app_preview_result(
	struct matuwall_app *app, const struct matuwall_thumb_result *result);

void matuwall_app_preview_finish(struct matuwall_app *app);

#endif
