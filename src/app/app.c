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

static bool wait_for_configure(struct matuwall_app *app) {
	if (wl_display_roundtrip(app->display) < 0) {
		matuwall_log_error("wayland", "roundtrip failed");
		return false;
	}
	if (!app->layer.configured) {
		matuwall_log_error(
			"wayland", "compositor never configured the surface");
		return false;
	}
	return true;
}

bool matuwall_app_init(struct matuwall_app *app,
	const struct matuwall_config *config, const char *output_name) {
	*app = (struct matuwall_app){
		.config = *config,
		.layout = config->layout,
		.visible_rows = config->visible_rows,
		.instance =
			{
				.lock_fd = -1,
				.socket_fd = -1,
			},
		.running = true,
	};
	matuwall_animation_init(&app->animation, app->config.navigation_ms,
		app->config.zoom_percent);

	if (!matuwall_app_loop_install_signals()) {
		matuwall_log_error(
			"startup", "failed to install signal handlers");
		return false;
	}
	if (!matuwall_instance_init(&app->instance)) {
		return false;
	}

	if (app->config.directory[0] == '\0') {
		matuwall_log_error(
			"wallpapers", "no directory set (is HOME set?)");
		return false;
	}
	if (!matuwall_dirscan_run(&app->scan, app->config.directory)) {
		return false;
	}
	matuwall_grid_init(&app->grid, app->scan.count);
	matuwall_log_info("wallpapers", "found %zu image%s in %s",
		app->scan.count, app->scan.count == 1 ? "" : "s",
		app->config.directory);

	app->display = wl_display_connect(NULL);
	if (app->display == NULL) {
		matuwall_log_error("wayland", "cannot connect to a compositor "
					      "(is WAYLAND_DISPLAY set?)");
		return false;
	}

	if (!matuwall_registry_init(
		    &app->registry, app->display, output_name)) {
		return false;
	}

	const struct matuwall_seat_handler *input_handler =
		matuwall_app_input_handler(app->config.mouse_enabled);
	if (!matuwall_seat_init(
		    &app->seat, app->registry.seat, input_handler, app)) {
		return false;
	}
	if (!matuwall_registry_select_output(&app->registry, app->display)) {
		return false;
	}

	if (!matuwall_layer_create(&app->layer, &app->registry,
		    app->registry.selected_output)) {
		matuwall_log_error(
			"wayland", "failed to create the layer surface");
		return false;
	}

	// The bufferless output probe gives the compositor-selected bounds
	if (!wait_for_configure(app)) {
		return false;
	}
	app->output_width = app->layer.width;
	app->output_height = app->layer.height;
	matuwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, &app->layout,
		&app->visible_rows);
	matuwall_log_info("output", "%ux%u, grid %ux%u, preview %s",
		app->output_width, app->output_height, app->layout.columns,
		app->visible_rows, app->config.preview ? "on" : "off");

	if (!app->config.preview) {
		uint32_t width;
		uint32_t height;
		matuwall_layout_surface_size(&app->layout, app->scan.count,
			app->visible_rows, &width, &height);
		matuwall_layer_set_panel(
			&app->layer, width, height, app->config.position);
		if (!wait_for_configure(app)) {
			return false;
		}
	}
	return true;
}

void matuwall_app_finish(struct matuwall_app *app) {
	matuwall_layer_destroy(&app->layer);
	matuwall_seat_finish(&app->seat);
	matuwall_registry_finish(&app->registry);
	if (app->display != NULL) {
		wl_display_disconnect(app->display);
		app->display = NULL;
	}
	matuwall_instance_finish(&app->instance);

	matuwall_app_thumbs_finish(app);
	matuwall_app_preview_finish(app);
	matuwall_dirscan_finish(&app->scan);
	app->running = false;
}
