#include "app/loop.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

#include "app/app.h"
#include "app/preview.h"
#include "app/thumbs.h"
#include "backend/backend.h"
#include "hooks/hooks.h"
#include "render/frame.h"
#include "render/spinner.h"
#include "state/selection.h"
#include "util/clock.h"
#include "util/log.h"

// The run phase. Owns no lifetimes: app.c builds and tears down every
// subsystem this file drives

// Set from a signal handler; only ever read as a flag by the event loop
static volatile sig_atomic_t interrupted = 0;

static void handle_signal(int signum) {
	(void)signum;
	interrupted = 1;
}

bool sweetwall_app_loop_install_signals(void) {
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

// --- frames ---

// The panel is the whole surface unless a backdrop is drawn around it
static void refresh_panel(struct sweetwall_app *app) {
	if (app->config.preview) {
		app->output_width = app->layer.width;
		app->output_height = app->layer.height;
	}
	sweetwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, &app->layout,
		&app->visible_rows);
	app->panel = sweetwall_layout_panel(&app->layout, app->scan.count,
		app->visible_rows, app->config.position, app->layer.width,
		app->layer.height);
	sweetwall_grid_reveal(
		&app->grid, &app->layout, (uint32_t)app->panel.height);
	sweetwall_app_thumbs_prioritize_visible(app);
}

// Draw only when something actually changed
static bool render_if_needed(struct sweetwall_app *app) {
	if (!app->layer.needs_repaint || !app->layer.configured) {
		return true;
	}

	struct sweetwall_buffer *buffer =
		sweetwall_layer_begin_frame(&app->layer, app->registry.shm);
	if (buffer == NULL) {
		sweetwall_log_error(
			"render", "failed to acquire a frame buffer");
		return false;
	}

	double scale = app->layer.width > 0
			       ? (double)buffer->width / app->layer.width
			       : 1.0;

	int32_t step = (int32_t)(app->layout.tile_height + app->layout.spacing);

	struct sweetwall_frame frame = {
		.layout = &app->layout,
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
		.ring_width = app->config.ring_width,
		.spinner = app->config.spinner,
		.spinner_alpha = sweetwall_spinner_alpha(sweetwall_now_ms()),
	};
	sweetwall_frame_draw(buffer, &frame);
	sweetwall_layer_commit_frame(&app->layer);
	return true;
}

static void restore_selection(struct sweetwall_app *app) {
	char path[PATH_MAX];
	if (!sweetwall_selection_load(path, sizeof(path))) {
		return;
	}
	for (size_t i = 0; i < app->scan.count; i++) {
		if (strcmp(path, app->scan.paths[i]) != 0) {
			continue;
		}
		if (sweetwall_grid_select(&app->grid, &app->layout,
			    (uint32_t)app->panel.height, i)) {
			app->layer.needs_repaint = true;
		}
		sweetwall_log_info("state", "restored selection %s", path);
		return;
	}
	sweetwall_log_info(
		"state", "remembered wallpaper is no longer present");
}

// --- event loop ---

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
		sweetwall_log_error("wayland", "compositor disconnected");
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

// --- apply ---

// Hand the selected wallpaper to the backend; runs off the input path on exit
static bool apply_selection(struct sweetwall_app *app) {
	const char *path = app->scan.paths[app->grid.selected];
	const struct sweetwall_backend *backend =
		sweetwall_backend_select(app->config.backend);
	if (backend == NULL) {
		if (strcmp(app->config.backend, "auto") == 0) {
			sweetwall_log_error("backend",
				"no running wallpaper backend found");
		} else {
			sweetwall_log_error("backend", "unknown backend '%s'",
				app->config.backend);
		}
		return false;
	}
	if (strcmp(app->config.backend, "auto") != 0) {
		sweetwall_log_info("backend", "selected configured backend %s",
			backend->name);
	}
	if (!backend->apply(path)) {
		sweetwall_log_error("backend",
			"%s failed to apply the wallpaper", backend->name);
		return false;
	}
	if (!sweetwall_selection_save(path)) {
		sweetwall_log_warn("state", "could not remember the selection");
	}
	sweetwall_log_info("apply", "applied %s with %s", path, backend->name);
	sweetwall_hooks_run(&app->config, path);
	return true;
}

bool sweetwall_app_run(struct sweetwall_app *app) {
	// The configured surface size is only known now, and every later
	// configure refreshes this again from pump_events
	refresh_panel(app);

	// First frame before any decoding: placeholders only
	app->layer.needs_repaint = true;
	if (!render_if_needed(app)) {
		return false;
	}
	if (!sweetwall_log_activate()) {
		sweetwall_log_warn("logging", "persistent log is unavailable");
	}

	// Persistent state stays off the first-frame path
	restore_selection(app);
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
	if (interrupted != 0) {
		sweetwall_log_info("exit", "stopped by signal");
	} else if (app->layer.closed) {
		sweetwall_log_warn("exit", "surface closed by compositor");
	}

	// Enter requested an apply: do it now, off the input path, on the way
	// out
	if (app->apply_requested) {
		return apply_selection(app);
	}
	return true;
}
