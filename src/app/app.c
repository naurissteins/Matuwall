#include "app/app.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>

#include "app/apply.h"
#include "app/fit.h"
#include "app/input.h"
#include "app/preview.h"
#include "app/thumbs.h"
#include "util/clock.h"
#include "util/log.h"

// Lifecycle only: build every subsystem, tear every one back down. The run
// phase lives in loop.c

// layer-shell promises a configure, not that it beats the roundtrip
#define CONFIGURE_TIMEOUT_MS 1000

static bool wait_for_configure(
	struct matuwall_app *app, const struct matuwall_layer *layer) {
	if (wl_display_roundtrip(app->display) < 0) {
		matuwall_log_error("wayland", "roundtrip failed");
		return false;
	}
	int64_t deadline = matuwall_now_ms() + CONFIGURE_TIMEOUT_MS;
	struct pollfd pfd = {
		.fd = wl_display_get_fd(app->display),
		.events = POLLIN,
	};
	while (!layer->configured && !layer->closed) {
		int64_t left = deadline - matuwall_now_ms();
		if (left <= 0) {
			break;
		}
		int ready = poll(&pfd, 1, (int)left);
		if ((ready < 0 && errno != EINTR) ||
			(ready > 0 && wl_display_dispatch(app->display) < 0)) {
			matuwall_log_error("wayland", "dispatch failed");
			return false;
		}
	}
	if (!layer->configured) {
		matuwall_log_error("wayland",
			"compositor did not provide usable surface dimensions");
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
		.initial_index = SIZE_MAX,
		.running = true,
	};
	matuwall_animation_init(&app->animation, app->config.navigation_ms,
		app->config.zoom_percent);

	if (!matuwall_instance_init(&app->instance)) {
		return false;
	}

	bool listed = app->config.list[0] != '\0';
	if (listed) {
		if (!matuwall_dirscan_list(&app->scan, app->config.list,
			    matuwall_config_directory(&app->config))) {
			return false;
		}
	} else if (!matuwall_dirscan_run(&app->scan, &app->config.directories,
			   app->config.recursive)) {
		return false;
	}
	matuwall_grid_init(&app->grid, app->scan.count);
	if (!app->scan.unavailable) {
		char count[32];
		matuwall_log_info("wallpapers", "found %zu image%s in %s",
			app->scan.count, app->scan.count == 1 ? "" : "s",
			matuwall_config_source(
				&app->config, count, sizeof(count)));
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
	// the probe saw every initial output event, later ones are changes
	app->registry.outputs.changed = false;
	// mapped before the panel, since compositors stack layers by map order
	if (preview &&
		!matuwall_layer_map_clear(&app->backdrop, app->registry.shm)) {
		matuwall_log_warn(
			"preview", "cannot map the backdrop, preview disabled");
		matuwall_layer_destroy(&app->backdrop);
	}
	struct matuwall_layer_panel panel = matuwall_app_fit_panel(app);
	matuwall_log_info("output", "%ux%u, grid %ux%u, preview %s",
		app->output_width, app->output_height, app->layout.columns,
		app->visible_rows, preview ? "on" : "off");
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
	// the apply thread borrows config, join it before anything goes
	matuwall_app_apply_finish(app);
	matuwall_layer_destroy(&app->layer);
	matuwall_layer_destroy(&app->backdrop);
	matuwall_layer_destroy(&app->probe);
	matuwall_seat_finish(&app->seat);
	matuwall_registry_finish(&app->registry);
	if (app->display != NULL) {
		wl_display_disconnect(app->display);
		app->display = NULL;
	}
	matuwall_instance_finish(&app->instance);

	matuwall_app_thumbs_finish(app);
	matuwall_app_preview_finish(app);
	free(app->favorites);
	app->favorites = NULL;
	matuwall_dirscan_finish(&app->scan);
	app->running = false;
}
