#ifndef SWEETWALL_APP_APP_H
#define SWEETWALL_APP_APP_H

#include <stdbool.h>

#include <stddef.h>

#include "app/animation.h"
#include "app/instance.h"
#include "app/preview.h"
#include "config/config.h"
#include "grid/navigate.h"
#include "scan/dirscan.h"
#include "thumb/worker.h"
#include "wayland/layer.h"
#include "wayland/registry.h"
#include "wayland/seat.h"

struct sweetwall_app {
	struct wl_display *display;
	struct sweetwall_registry registry;
	struct sweetwall_layer layer;
	struct sweetwall_seat seat;
	struct sweetwall_instance instance;

	struct sweetwall_config config;
	// Effective grid after fitting the configured maxima to the output
	struct sweetwall_layout layout;
	uint32_t visible_rows;
	uint32_t output_width;
	uint32_t output_height;
	struct sweetwall_dirscan scan;
	struct sweetwall_grid grid;
	struct sweetwall_animation animation;
	// Panel geometry inside the surface; equal to the surface without
	// preview
	struct sweetwall_rect panel;
	struct sweetwall_preview preview;

	struct sweetwall_worker_pool *workers;
	struct sweetwall_thumb *thumbs;
	size_t thumb_count;
	size_t pending;
	size_t thumb_priority_first;
	size_t thumb_priority_end;
	size_t thumb_cache_hits;
	size_t thumb_decoded;
	size_t thumb_failed;
	bool thumb_priority_set;
	bool running;
	bool apply_requested;
};

bool sweetwall_app_init(struct sweetwall_app *app,
	const struct sweetwall_config *config, const char *output_name);

bool sweetwall_app_run(struct sweetwall_app *app);

void sweetwall_app_finish(struct sweetwall_app *app);

#endif
