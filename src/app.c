#include "app.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <wayland-client.h>

// TODO: replace these with config values once the config slice lands
#define DEFAULT_WIDTH 960
#define DEFAULT_HEIGHT 560
#define DEFAULT_POSITION SWEETWALL_POSITION_CENTER
static const struct sweetwall_color default_background = {
	.r = 0x1e, .g = 0x1e, .b = 0x2e, .a = 0xcc};

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

	// No SA_RESTART: poll() must return EINTR so the loop can notice
	if (sigaction(SIGINT, &action, NULL) != 0) {
		return false;
	}
	if (sigaction(SIGTERM, &action, NULL) != 0) {
		return false;
	}
	return true;
}

static void handle_key(void *user_data, xkb_keysym_t sym) {
	struct sweetwall_app *app = user_data;

	switch (sym) {
	case XKB_KEY_Escape:
		app->running = false;
		break;
	default:
		break;
	}
}

bool sweetwall_app_init(struct sweetwall_app *app) {
	*app = (struct sweetwall_app){
		.background = default_background,
		.running = true,
	};

	if (!install_signal_handlers()) {
		fprintf(stderr,
			"sweetwall: failed to install signal handlers\n");
		return false;
	}

	app->display = wl_display_connect(NULL);
	if (app->display == NULL) {
		fprintf(stderr, "sweetwall: cannot connect to a wayland "
				"compositor (is WAYLAND_DISPLAY set?)\n");
		return false;
	}

	if (!sweetwall_registry_init(&app->registry, app->display)) {
		return false;
	}

	if (!sweetwall_seat_init(
		    &app->seat, app->registry.seat, handle_key, app)) {
		return false;
	}

	if (!sweetwall_layer_create(&app->layer, &app->registry, DEFAULT_WIDTH,
		    DEFAULT_HEIGHT, DEFAULT_POSITION)) {
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

	return true;
}

// Draw only when something actually changed
static bool render_if_needed(struct sweetwall_app *app) {
	if (!app->layer.needs_repaint || !app->layer.configured) {
		return true;
	}
	uint32_t color = sweetwall_color_argb(app->background);
	if (!sweetwall_layer_paint_color(
		    &app->layer, app->registry.shm, color)) {
		fprintf(stderr, "sweetwall: failed to paint the surface\n");
		return false;
	}
	return true;
}

// One poll() over the display fd. The thumbnail worker fd joins it later
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

	struct pollfd pfd = {
		.fd = wl_display_get_fd(app->display),
		.events = POLLIN,
	};

	if (poll(&pfd, 1, -1) < 0) {
		wl_display_cancel_read(app->display);
		// A caught signal is a normal wakeup, not a failure
		return errno == EINTR;
	}

	if ((pfd.revents & (POLLERR | POLLHUP)) != 0) {
		wl_display_cancel_read(app->display);
		fprintf(stderr, "sweetwall: compositor disconnected\n");
		return false;
	}

	if (wl_display_read_events(app->display) < 0) {
		return false;
	}
	return wl_display_dispatch_pending(app->display) >= 0;
}

bool sweetwall_app_run(struct sweetwall_app *app) {
	app->layer.needs_repaint = true;
	if (!render_if_needed(app)) {
		return false;
	}

	while (app->running && !app->layer.closed && interrupted == 0) {
		if (!pump_events(app)) {
			return false;
		}
		if (!render_if_needed(app)) {
			return false;
		}
	}

	return true;
}

void sweetwall_app_finish(struct sweetwall_app *app) {
	sweetwall_layer_destroy(&app->layer);
	// The seat holds a wl_keyboard from the registry's wl_seat; it has to
	// go first
	sweetwall_seat_finish(&app->seat);
	sweetwall_registry_finish(&app->registry);
	if (app->display != NULL) {
		wl_display_disconnect(app->display);
		app->display = NULL;
	}
	app->running = false;
}
