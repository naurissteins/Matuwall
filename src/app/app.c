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

static bool wait_for_configure(
	struct matuwall_app *app, const struct matuwall_layer *layer) {
	if (wl_display_roundtrip(app->display) < 0) {
		matuwall_log_error("wayland", "roundtrip failed");
		return false;
	}
	if (!layer->configured) {
		matuwall_log_error("wayland",
			"compositor did not provide usable surface dimensions");
		return false;
	}
	return true;
}

// layer margin that places panel against its anchored edge
static uint32_t edge_offset(const struct matuwall_rect *panel,
	enum matuwall_position position, uint32_t width, uint32_t height) {
	switch (position) {
	case MATUWALL_POSITION_LEFT:
		return (uint32_t)panel->x;
	case MATUWALL_POSITION_RIGHT:
		return width - (uint32_t)(panel->x + panel->width);
	case MATUWALL_POSITION_TOP:
		return (uint32_t)panel->y;
	case MATUWALL_POSITION_BOTTOM:
		return height - (uint32_t)(panel->y + panel->height);
	case MATUWALL_POSITION_CENTER:
		break;
	}
	return 0;
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
		app->scan.unavailable = true;
		matuwall_log_error("wallpapers",
			"directory unavailable: path is "
			"unresolved (is HOME set?)");
	} else if (!matuwall_dirscan_run(&app->scan, app->config.directory)) {
		return false;
	}
	matuwall_grid_init(&app->grid, app->scan.count);
	if (!app->scan.unavailable) {
		matuwall_log_info("wallpapers", "found %zu image%s in %s",
			app->scan.count, app->scan.count == 1 ? "" : "s",
			app->config.directory);
	}

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

	// backdrop probes the output in preview mode
	bool preview = app->config.preview;
	struct matuwall_layer *probe = preview ? &app->backdrop : &app->layer;
	if (!matuwall_layer_create(probe, &app->registry,
		    app->registry.selected_output,
		    preview ? MATUWALL_LAYER_BACKDROP : MATUWALL_LAYER_PANEL,
		    NULL)) {
		matuwall_log_error(
			"wayland", "failed to create the layer surface");
		return false;
	}

	// bufferless output probe gives the compositor-selected bounds
	if (!wait_for_configure(app, probe)) {
		return false;
	}
	app->output_width = probe->width;
	app->output_height = probe->height;
	// mapped before the panel, since compositors stack layers by map order
	if (preview &&
		!matuwall_layer_map_clear(&app->backdrop, app->registry.shm)) {
		matuwall_log_warn(
			"preview", "cannot map the backdrop, preview disabled");
		matuwall_layer_destroy(&app->backdrop);
	}
	matuwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, app->config.carousel,
		app->config.position, app->config.edge_margin, &app->layout,
		&app->visible_rows);
	matuwall_log_info("output", "%ux%u, grid %ux%u, preview %s",
		app->output_width, app->output_height, app->layout.columns,
		app->visible_rows, preview ? "on" : "off");

	// off-screen rows cost buffer, so the panel surface fits the output
	struct matuwall_rect rect = matuwall_layout_panel(&app->layout,
		app->scan.count, app->visible_rows, app->config.position,
		app->config.edge_margin, app->output_width, app->output_height);
	struct matuwall_layer_panel panel = {
		.width = (uint32_t)rect.width,
		.height = (uint32_t)rect.height,
		.position = app->config.position,
		.margin = edge_offset(&rect, app->config.position,
			app->output_width, app->output_height),
	};
	if (!preview) {
		matuwall_layer_set_panel(&app->layer, &panel);
	} else if (!matuwall_layer_create(&app->layer, &app->registry,
			   app->registry.selected_output, MATUWALL_LAYER_PANEL,
			   &panel)) {
		matuwall_log_error(
			"wayland", "failed to create the panel surface");
		return false;
	}
	return wait_for_configure(app, &app->layer);
}

void matuwall_app_finish(struct matuwall_app *app) {
	matuwall_layer_destroy(&app->layer);
	matuwall_layer_destroy(&app->backdrop);
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
