#ifndef MATUWALL_APP_APP_H
#define MATUWALL_APP_APP_H

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

struct matuwall_app {
	struct wl_display *display;
	struct matuwall_registry registry;
	struct matuwall_layer layer;
	struct matuwall_seat seat;
	struct matuwall_instance instance;

	struct matuwall_config config;
	// Effective grid after fitting the configured maxima to the output
	struct matuwall_layout layout;
	uint32_t visible_rows;
	uint32_t output_width;
	uint32_t output_height;
	struct matuwall_dirscan scan;
	struct matuwall_grid grid;
	struct matuwall_animation animation;
	// Panel geometry inside the surface; equal to the surface without
	// preview
	struct matuwall_rect panel;
	struct matuwall_preview preview;

	struct matuwall_worker_pool *workers;
	struct matuwall_thumb *thumbs;
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

bool matuwall_app_init(struct matuwall_app *app,
	const struct matuwall_config *config, const char *output_name);

bool matuwall_app_run(struct matuwall_app *app);

void matuwall_app_finish(struct matuwall_app *app);

#endif
