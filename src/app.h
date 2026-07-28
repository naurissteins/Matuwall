#ifndef SWEETWALL_APP_H
#define SWEETWALL_APP_H

#include <stdbool.h>

#include <stddef.h>

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

	struct sweetwall_config config;
	struct sweetwall_dirscan scan;
	struct sweetwall_grid grid;

	struct sweetwall_worker_pool *workers;
	struct sweetwall_thumb *thumbs;
	size_t thumb_count;
	size_t pending;
	bool running;
};

bool sweetwall_app_init(
	struct sweetwall_app *app, const struct sweetwall_config *config);

bool sweetwall_app_run(struct sweetwall_app *app);

void sweetwall_app_finish(struct sweetwall_app *app);

#endif
