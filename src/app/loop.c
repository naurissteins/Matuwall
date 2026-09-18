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

bool matuwall_app_loop_install_signals(void) {
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

static bool same_layout(
	const struct matuwall_layout *a, const struct matuwall_layout *b) {
	return a->columns == b->columns && a->spacing == b->spacing &&
	       a->margin == b->margin && a->tile_width == b->tile_width &&
	       a->tile_height == b->tile_height && a->radius == b->radius &&
	       a->flow == b->flow;
}

static bool same_rect(
	const struct matuwall_rect *a, const struct matuwall_rect *b) {
	return a->x == b->x && a->y == b->y && a->width == b->width &&
	       a->height == b->height;
}

// The panel is the whole surface unless a backdrop is drawn around it
static void refresh_panel(struct matuwall_app *app) {
	struct matuwall_layout old_layout = app->layout;
	struct matuwall_rect old_panel = app->panel;
	uint32_t old_first_row = app->grid.first_row;
	if (app->config.preview) {
		app->output_width = app->layer.width;
		app->output_height = app->layer.height;
	}
	matuwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, app->config.carousel,
		app->config.position, app->config.edge_margin, &app->layout,
		&app->visible_rows);
	app->panel = matuwall_layout_panel(&app->layout, app->scan.count,
		app->visible_rows, app->config.position,
		app->config.edge_margin, app->layer.width, app->layer.height);
	matuwall_grid_reveal(
		&app->grid, &app->layout, (uint32_t)app->panel.height);
	if (!same_layout(&old_layout, &app->layout) ||
		!same_rect(&old_panel, &app->panel) ||
		old_first_row != app->grid.first_row) {
		matuwall_animation_snap(&app->animation, &app->layout,
			&app->panel, app->grid.selected, app->grid.cursor,
			app->grid.first_row);
	}
	matuwall_app_thumbs_prioritize_visible(app);
	app->layer.layout_dirty = false;
}

// Draw only when something actually changed
static bool render_if_needed(struct matuwall_app *app) {
	if (!app->layer.needs_repaint || !app->layer.configured) {
		return true;
	}

	struct matuwall_buffer *buffer;
	enum matuwall_buffer_acquire acquired = matuwall_layer_begin_frame(
		&app->layer, app->registry.shm, &buffer);
	if (acquired == MATUWALL_BUFFER_BUSY) {
		return true;
	}
	if (acquired == MATUWALL_BUFFER_FAILED) {
		matuwall_log_error(
			"render", "failed to acquire a frame buffer");
		return false;
	}

	double scale = app->layer.width > 0
			       ? (double)buffer->width / app->layer.width
			       : 1.0;

	struct matuwall_animation_sample visual;
	matuwall_animation_sample(&app->animation, matuwall_now_ms(), &visual);

	struct matuwall_frame frame = {
		.layout = &app->layout,
		.thumbs = app->thumbs,
		.item_count = app->scan.count,
		.carousel_slot = app->grid.cursor,
		.scroll = visual.scroll,
		.panel = app->panel,
		.backdrop = app->config.preview,
		.preview = app->preview.image.pixels,
		.preview_width = app->preview.image.width,
		.preview_height = app->preview.image.height,
		.directory_unavailable = app->scan.unavailable,
		.edge_peek = app->config.edge_peek,
		.scale = scale,
		.background = matuwall_color_argb(app->config.background),
		.panel_radius = app->config.panel_radius,
		.tile = matuwall_color_argb(app->config.tile),
		.border = matuwall_color_argb(app->config.border),
		.border_width = app->config.border_width,
		.shadow = matuwall_color_argb(app->config.shadow),
		.shadow_width = app->config.shadow_width,
		.focus_scale = 1.0 + (double)app->config.zoom_percent / 100.0,
		.ring = app->config.ring,
		.ring_width = app->config.ring_width,
		.spinner = app->config.spinner,
		.spinner_alpha = matuwall_spinner_alpha(matuwall_now_ms()),
	};
	if (app->scan.count > 0) {
		frame.ring_count = visual.ring_count;
		for (size_t i = 0; i < visual.ring_count; i++) {
			frame.rings[i] = (struct matuwall_frame_ring){
				.x = visual.rings[i].rect.x,
				.y = visual.rings[i].rect.y,
				.width = visual.rings[i].rect.width,
				.height = visual.rings[i].rect.height,
				.alpha = visual.rings[i].alpha,
			};
		}
		frame.focus_count = visual.focus_count;
		for (size_t i = 0; i < visual.focus_count; i++) {
			frame.focuses[i] = (struct matuwall_frame_focus){
				.index = visual.focuses[i].index,
				.slot = visual.focuses[i].slot,
				.scale = visual.focuses[i].scale,
			};
		}
	}
	struct matuwall_damage damage =
		matuwall_frame_draw(buffer, &frame, app->preview.generation);
	bool opaque = frame.backdrop && frame.preview != NULL;
	if (!matuwall_layer_commit_frame(
		    &app->layer, visual.active, opaque, &damage)) {
		matuwall_log_error("render", "failed to commit a frame");
		return false;
	}
	return true;
}

static void restore_selection(struct matuwall_app *app) {
	char path[PATH_MAX];
	if (!matuwall_selection_load(path, sizeof(path))) {
		return;
	}
	for (size_t i = 0; i < app->scan.count; i++) {
		if (strcmp(path, app->scan.paths[i]) != 0) {
			continue;
		}
		if (matuwall_grid_select(&app->grid, &app->layout,
			    (uint32_t)app->panel.height, i, (int64_t)i)) {
			matuwall_animation_snap(&app->animation, &app->layout,
				&app->panel, app->grid.selected,
				app->grid.cursor, app->grid.first_row);
			app->layer.needs_repaint = true;
		}
		matuwall_log_info("state", "restored selection %s", path);
		return;
	}
	matuwall_log_info("state", "remembered wallpaper is no longer present");
}

// --- event loop ---

// Merge poll deadlines; -1 means "no deadline of my own"
static int sooner(int timeout, int candidate) {
	if (candidate < 0) {
		return timeout;
	}
	return timeout < 0 || candidate < timeout ? candidate : timeout;
}

static bool pump_events(struct matuwall_app *app) {
	while (wl_display_prepare_read(app->display) != 0) {
		if (wl_display_dispatch_pending(app->display) < 0) {
			return false;
		}
	}

	if (wl_display_flush(app->display) < 0 && errno != EAGAIN) {
		wl_display_cancel_read(app->display);
		return false;
	}

	struct pollfd pfd[3] = {
		{.fd = wl_display_get_fd(app->display), .events = POLLIN},
		{.fd = matuwall_instance_fd(&app->instance), .events = POLLIN},
	};
	nfds_t nfds = 2;
	nfds_t worker_index = 0;
	if (app->workers != NULL) {
		worker_index = nfds;
		pfd[worker_index].fd = matuwall_worker_pool_fd(app->workers);
		pfd[worker_index].events = POLLIN;
		nfds++;
	}

	int timeout = matuwall_seat_repeat_timeout(&app->seat);
	// Pulse only while an on-screen thumbnail is pending
	if (app->visible_pending > 0) {
		timeout = sooner(timeout, MATUWALL_SPINNER_INTERVAL_MS);
	}
	timeout = sooner(
		timeout, matuwall_app_preview_timeout(app, matuwall_now_ms()));

	if (poll(pfd, nfds, timeout) < 0) {
		wl_display_cancel_read(app->display);
		// A caught signal is a normal wakeup, not a failure
		return errno == EINTR;
	}

	if ((pfd[0].revents & (POLLERR | POLLHUP)) != 0) {
		wl_display_cancel_read(app->display);
		matuwall_log_error("wayland", "compositor disconnected");
		return false;
	}
	if ((pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
		wl_display_cancel_read(app->display);
		matuwall_log_error("instance", "runtime socket failed");
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
	if (app->layer.layout_dirty) {
		refresh_panel(app);
	}

	if ((pfd[1].revents & POLLIN) != 0) {
		bool replace_requested;
		if (!matuwall_instance_dispatch(
			    &app->instance, &replace_requested)) {
			return false;
		}
		if (replace_requested) {
			matuwall_log_info(
				"exit", "replaced by a newer matuwall launch");
			app->running = false;
		}
	}
	if (worker_index > 0 && (pfd[worker_index].revents & POLLIN) != 0) {
		matuwall_app_thumbs_drain(app);
	}

	matuwall_seat_dispatch_repeat(&app->seat);
	matuwall_app_preview_tick(app, matuwall_now_ms());
	// Keep the pulse advancing while visible tiles are still loading
	if (app->visible_pending > 0) {
		app->layer.needs_repaint = true;
	}
	matuwall_layer_collect_idle(&app->layer);
	return true;
}

// --- apply ---

// Hand the selected wallpaper to the backend; runs off the input path on exit
static bool apply_selection(struct matuwall_app *app) {
	const char *path = app->scan.paths[app->grid.selected];
	const struct matuwall_backend *backend =
		matuwall_backend_select(app->config.backend);
	if (backend == NULL) {
		if (strcmp(app->config.backend, "auto") == 0) {
			matuwall_log_error("backend",
				"no running wallpaper backend found");
		} else {
			matuwall_log_error("backend", "unknown backend '%s'",
				app->config.backend);
		}
		return false;
	}
	if (strcmp(app->config.backend, "auto") != 0) {
		matuwall_log_info("backend", "selected configured backend %s",
			backend->name);
	}
	if (!backend->apply(path)) {
		matuwall_log_error("backend",
			"%s failed to apply the wallpaper", backend->name);
		return false;
	}
	if (!matuwall_selection_save(path)) {
		matuwall_log_warn("state", "could not remember the selection");
	}
	matuwall_log_info("apply", "applied %s with %s", path, backend->name);
	matuwall_hooks_run(&app->config, path);
	return true;
}

bool matuwall_app_run(struct matuwall_app *app) {
	// The configured surface size is only known now, and every later
	// configure refreshes this again from pump_events
	refresh_panel(app);

	// First frame before any decoding: placeholders only
	app->layer.needs_repaint = true;
	if (!render_if_needed(app)) {
		return false;
	}
	if (!matuwall_log_activate()) {
		matuwall_log_warn("logging", "persistent log is unavailable");
	}

	// Persistent state stays off the first-frame path
	restore_selection(app);
	matuwall_app_thumbs_start(app);
	matuwall_app_preview_init(app);
	matuwall_app_preview_select(app, app->grid.selected, matuwall_now_ms());

	while (app->running && !app->layer.closed && interrupted == 0) {
		if (!pump_events(app)) {
			return false;
		}
		if (!app->running) {
			break;
		}
		if (!render_if_needed(app)) {
			return false;
		}
	}
	if (interrupted != 0) {
		matuwall_log_info("exit", "stopped by signal");
	} else if (app->layer.closed) {
		matuwall_log_warn("exit", "surface closed by compositor");
	}

	// Enter requested an apply: do it now, off the input path, on the way
	// out
	if (app->apply_requested) {
		return apply_selection(app);
	}
	return !app->scan.unavailable;
}
