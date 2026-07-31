#include "app.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

#include "app_input.h"
#include "app_preview.h"
#include "app_thumbs.h"
#include "backend/backend.h"
#include "hooks/hooks.h"
#include "render/frame.h"
#include "render/spinner.h"
#include "util/clock.h"

// Set from a signal handler; only ever read as a flag by the event loop
static volatile sig_atomic_t interrupted = 0;

static void handle_signal(int signum) {
	(void)signum;
	interrupted = 1;
}

static bool install_signal_handlers(void) {
	struct sigaction action = {
		.sa_handler = handle_signal,
	};
	sigemptyset(&action.sa_mask);

	if (sigaction(SIGINT, &action, NULL) != 0) {
		return false;
	}
	if (sigaction(SIGTERM, &action, NULL) != 0) {
		return false;
	}
	return true;
}

// The panel is the whole surface unless a backdrop is drawn around it
static void refresh_panel(struct sweetwall_app *app) {
	app->panel = sweetwall_layout_panel(&app->config.layout,
		app->scan.count, app->config.visible_rows, app->config.position,
		app->layer.width, app->layer.height);
}

bool sweetwall_app_init(
	struct sweetwall_app *app, const struct sweetwall_config *config) {
	*app = (struct sweetwall_app){
		.config = *config,
		.running = true,
	};

	if (!install_signal_handlers()) {
		fprintf(stderr,
			"sweetwall: failed to install signal handlers\n");
		return false;
	}

	if (app->config.directory[0] == '\0') {
		fprintf(stderr, "sweetwall: no wallpaper directory set "
				"(is HOME set?)\n");
		return false;
	}
	if (!sweetwall_dirscan_run(&app->scan, app->config.directory)) {
		return false;
	}
	sweetwall_grid_init(&app->grid, app->scan.count);

	app->display = wl_display_connect(NULL);
	if (app->display == NULL) {
		fprintf(stderr, "sweetwall: cannot connect to a wayland "
				"compositor (is WAYLAND_DISPLAY set?)\n");
		return false;
	}

	if (!sweetwall_registry_init(&app->registry, app->display)) {
		return false;
	}

	if (!sweetwall_seat_init(&app->seat, app->registry.seat,
		    &sweetwall_app_seat_handler, app)) {
		return false;
	}

	uint32_t width;
	uint32_t height;
	sweetwall_layout_surface_size(&app->config.layout, app->scan.count,
		app->config.visible_rows, &width, &height);

	if (!sweetwall_layer_create(&app->layer, &app->registry, width, height,
		    app->config.position, app->config.preview)) {
		fprintf(stderr, "sweetwall: failed to create the layer "
				"surface\n");
		return false;
	}

	// Wait for the first configure so the surface has a real size
	if (wl_display_roundtrip(app->display) < 0) {
		fprintf(stderr, "sweetwall: wayland roundtrip failed\n");
		return false;
	}
	if (!app->layer.configured) {
		fprintf(stderr,
			"sweetwall: compositor never configured the surface\n");
		return false;
	}
	refresh_panel(app);

	return true;
}

// Draw only when something actually changed
static bool render_if_needed(struct sweetwall_app *app) {
	if (!app->layer.needs_repaint || !app->layer.configured) {
		return true;
	}

	struct sweetwall_buffer *buffer =
		sweetwall_layer_begin_frame(&app->layer, app->registry.shm);
	if (buffer == NULL) {
		fprintf(stderr,
			"sweetwall: failed to acquire a frame buffer\n");
		return false;
	}

	double scale = app->layer.width > 0
			       ? (double)buffer->width / app->layer.width
			       : 1.0;

	sweetwall_grid_reveal(
		&app->grid, &app->config.layout, (uint32_t)app->panel.height);

	int32_t step = (int32_t)(app->config.layout.tile_height +
				 app->config.layout.spacing);

	struct sweetwall_frame frame = {
		.layout = &app->config.layout,
		.thumbs = app->thumbs,
		.item_count = app->scan.count,
		.selected = app->grid.selected,
		.scroll = (int32_t)app->grid.first_row * step,
		.panel = app->panel,
		.backdrop = app->config.preview,
		.preview = app->preview.image.pixels,
		.preview_width = app->preview.image.width,
		.preview_height = app->preview.image.height,
		.scale = scale,
		.background = sweetwall_color_argb(app->config.background),
		.tile = sweetwall_color_argb(app->config.tile),
		.ring = sweetwall_color_argb(app->config.ring),
		.spinner = app->config.spinner,
		.spinner_alpha = sweetwall_spinner_alpha(sweetwall_now_ms()),
	};
	sweetwall_frame_draw(buffer, &frame);
	sweetwall_layer_commit_frame(&app->layer);
	return true;
}

// Merge poll deadlines; -1 means "no deadline of my own"
static int sooner(int timeout, int candidate) {
	if (candidate < 0) {
		return timeout;
	}
	return timeout < 0 || candidate < timeout ? candidate : timeout;
}

static bool pump_events(struct sweetwall_app *app) {
	while (wl_display_prepare_read(app->display) != 0) {
		if (wl_display_dispatch_pending(app->display) < 0) {
			return false;
		}
	}

	if (wl_display_flush(app->display) < 0 && errno != EAGAIN) {
		wl_display_cancel_read(app->display);
		return false;
	}

	struct pollfd pfd[2] = {
		{.fd = wl_display_get_fd(app->display), .events = POLLIN},
	};
	nfds_t nfds = 1;
	if (app->workers != NULL) {
		pfd[1].fd = sweetwall_worker_pool_fd(app->workers);
		pfd[1].events = POLLIN;
		nfds = 2;
	}

	int timeout = sweetwall_seat_repeat_timeout(&app->seat);
	// Pulse the loading dot while any thumbnail is still pending
	if (app->pending > 0) {
		timeout = sooner(timeout, SWEETWALL_SPINNER_INTERVAL_MS);
	}
	timeout = sooner(timeout,
		sweetwall_app_preview_timeout(app, sweetwall_now_ms()));

	if (poll(pfd, nfds, timeout) < 0) {
		wl_display_cancel_read(app->display);
		// A caught signal is a normal wakeup, not a failure
		return errno == EINTR;
	}

	if ((pfd[0].revents & (POLLERR | POLLHUP)) != 0) {
		wl_display_cancel_read(app->display);
		fprintf(stderr, "sweetwall: compositor disconnected\n");
		return false;
	}

	// The prepare_read must be matched by exactly one read or cancel
	if ((pfd[0].revents & POLLIN) != 0) {
		if (wl_display_read_events(app->display) < 0) {
			return false;
		}
	} else {
		wl_display_cancel_read(app->display);
	}
	if (wl_display_dispatch_pending(app->display) < 0) {
		return false;
	}
	// A configure may have resized the surface under the panel
	refresh_panel(app);

	if (nfds == 2 && (pfd[1].revents & POLLIN) != 0) {
		sweetwall_app_thumbs_drain(app);
	}

	sweetwall_seat_dispatch_repeat(&app->seat);
	sweetwall_app_preview_tick(app, sweetwall_now_ms());
	// Keep the pulse advancing while tiles are still loading
	if (app->pending > 0) {
		app->layer.needs_repaint = true;
	}
	return true;
}

// Hand the selected wallpaper to the backend; runs off the input path on exit
static bool apply_selection(struct sweetwall_app *app) {
	const char *path = app->scan.paths[app->grid.selected];
	const struct sweetwall_backend *backend =
		sweetwall_backend_select(app->config.backend);
	if (backend == NULL) {
		if (strcmp(app->config.backend, "auto") == 0) {
			fprintf(stderr,
				"sweetwall: no wallpaper backend found\n");
		} else {
			fprintf(stderr, "sweetwall: unknown backend '%s'\n",
				app->config.backend);
		}
		return false;
	}
	if (!backend->apply(path)) {
		fprintf(stderr, "sweetwall: %s failed to apply the wallpaper\n",
			backend->name);
		return false;
	}
	sweetwall_hooks_run(&app->config, path);
	return true;
}

bool sweetwall_app_run(struct sweetwall_app *app) {
	// First frame before any decoding: placeholders only
	app->layer.needs_repaint = true;
	if (!render_if_needed(app)) {
		return false;
	}

	sweetwall_app_thumbs_start(app);
	sweetwall_app_preview_init(app);
	sweetwall_app_preview_select(
		app, app->grid.selected, sweetwall_now_ms());

	while (app->running && !app->layer.closed && interrupted == 0) {
		if (!pump_events(app)) {
			return false;
		}
		if (!render_if_needed(app)) {
			return false;
		}
	}

	// Enter requested an apply: do it now, off the input path, on the way
	// out
	if (app->apply_requested) {
		return apply_selection(app);
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
