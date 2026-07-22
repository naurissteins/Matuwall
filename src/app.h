#ifndef SWEETWALL_APP_H
#define SWEETWALL_APP_H

#include <stdbool.h>

#include "grid/layout.h"
#include "grid/navigate.h"
#include "render/color.h"
#include "scan/dirscan.h"
#include "wayland/layer.h"
#include "wayland/registry.h"
#include "wayland/seat.h"

struct sweetwall_app {
	struct wl_display *display;
	struct sweetwall_registry registry;
	struct sweetwall_layer layer;
	struct sweetwall_seat seat;

	struct sweetwall_dirscan scan;
	struct sweetwall_layout layout;
	struct sweetwall_grid grid;

	struct sweetwall_color background;
	struct sweetwall_color tile;
	struct sweetwall_color ring;
	bool running;
};

// Connect to the compositor and map the layer surface
bool sweetwall_app_init(struct sweetwall_app *app);

// Poll until the user quits or the compositor closes the surface
bool sweetwall_app_run(struct sweetwall_app *app);

// Tear down every proxy and the connection. Safe after a partial init
void sweetwall_app_finish(struct sweetwall_app *app);

#endif
