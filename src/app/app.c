#include "app/app.h"

#include <stdio.h>
#include <wayland-client.h>

#include "app/input.h"
#include "app/loop.h"
#include "app/preview.h"
#include "app/thumbs.h"
#include "util/log.h"

// Lifecycle only: build every subsystem, tear every one back down. The run
// phase lives in loop.c

static bool wait_for_configure(struct sweetwall_app *app) {
	if (wl_display_roundtrip(app->display) < 0) {
		sweetwall_log_error("wayland", "roundtrip failed");
		return false;
	}
	if (!app->layer.configured) {
		sweetwall_log_error(
			"wayland", "compositor never configured the surface");
		return false;
	}
	return true;
}

bool sweetwall_app_init(struct sweetwall_app *app,
	const struct sweetwall_config *config, const char *output_name) {
	*app = (struct sweetwall_app){
		.config = *config,
		.layout = config->layout,
		.visible_rows = config->visible_rows,
		.running = true,
	};

	if (!sweetwall_app_loop_install_signals()) {
		sweetwall_log_error(
			"startup", "failed to install signal handlers");
		return false;
	}

	if (app->config.directory[0] == '\0') {
		sweetwall_log_error(
			"wallpapers", "no directory set (is HOME set?)");
		return false;
	}
	if (!sweetwall_dirscan_run(&app->scan, app->config.directory)) {
		return false;
	}
	sweetwall_grid_init(&app->grid, app->scan.count);
	sweetwall_log_info("wallpapers", "found %zu image%s in %s",
		app->scan.count, app->scan.count == 1 ? "" : "s",
		app->config.directory);

	app->display = wl_display_connect(NULL);
	if (app->display == NULL) {
		sweetwall_log_error("wayland", "cannot connect to a compositor "
					       "(is WAYLAND_DISPLAY set?)");
		return false;
	}

	if (!sweetwall_registry_init(
		    &app->registry, app->display, output_name)) {
		return false;
	}

	const struct sweetwall_seat_handler *input_handler =
		sweetwall_app_input_handler(app->config.mouse_enabled);
	if (!sweetwall_seat_init(
		    &app->seat, app->registry.seat, input_handler, app)) {
		return false;
	}
	if (!sweetwall_registry_select_output(&app->registry, app->display)) {
		return false;
	}

	if (!sweetwall_layer_create(&app->layer, &app->registry,
		    app->registry.selected_output)) {
		sweetwall_log_error(
			"wayland", "failed to create the layer surface");
		return false;
	}

	// The bufferless output probe gives the compositor-selected bounds
	if (!wait_for_configure(app)) {
		return false;
	}
	app->output_width = app->layer.width;
	app->output_height = app->layer.height;
	sweetwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, &app->layout,
		&app->visible_rows);
	sweetwall_log_info("output", "%ux%u, grid %ux%u, preview %s",
		app->output_width, app->output_height, app->layout.columns,
		app->visible_rows, app->config.preview ? "on" : "off");

	if (!app->config.preview) {
		uint32_t width;
		uint32_t height;
		sweetwall_layout_surface_size(&app->layout, app->scan.count,
			app->visible_rows, &width, &height);
		sweetwall_layer_set_panel(
			&app->layer, width, height, app->config.position);
		if (!wait_for_configure(app)) {
			return false;
		}
	}
	return true;
}

void sweetwall_app_finish(struct sweetwall_app *app) {
	sweetwall_app_thumbs_finish(app);
	sweetwall_app_preview_finish(app);

	sweetwall_layer_destroy(&app->layer);
	sweetwall_seat_finish(&app->seat);
	sweetwall_registry_finish(&app->registry);
	if (app->display != NULL) {
		wl_display_disconnect(app->display);
		app->display = NULL;
	}
	sweetwall_dirscan_finish(&app->scan);
	app->running = false;
}
