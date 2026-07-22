#include "app.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "render/frame.h"

// TODO: replace these with config values once the config slice lands
#define DEFAULT_POSITION SWEETWALL_POSITION_CENTER
#define DEFAULT_DIRECTORY "Pictures/Wallpapers"
// Caps the surface height; the compositor shrinks it further if it must
#define MAX_VISIBLE_ROWS 2

static const struct sweetwall_layout default_layout = {
	.columns = 5,
	.spacing = 16,
	.margin = 24,
	.tile_width = 240,
	.tile_height = 400,
	.radius = 8,
};
static const struct sweetwall_color default_background = {
	.r = 0x1e, .g = 0x1e, .b = 0x2e, .a = 0xcc};
static const struct sweetwall_color default_tile = {
	.r = 0x31, .g = 0x32, .b = 0x44, .a = 0xff};
static const struct sweetwall_color default_ring = {
	.r = 0xf2, .g = 0xcd, .b = 0xcd, .a = 0xff};

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

// Key policy
static void handle_key(void *user_data, xkb_keysym_t sym) {
	struct sweetwall_app *app = user_data;
	enum sweetwall_move move;

	switch (sym) {
	case XKB_KEY_Escape:
		app->running = false;
		return;
	case XKB_KEY_Left:
	case XKB_KEY_h:
		move = SWEETWALL_MOVE_LEFT;
		break;
	case XKB_KEY_Right:
	case XKB_KEY_l:
		move = SWEETWALL_MOVE_RIGHT;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_k:
		move = SWEETWALL_MOVE_UP;
		break;
	case XKB_KEY_Down:
	case XKB_KEY_j:
		move = SWEETWALL_MOVE_DOWN;
		break;
	case XKB_KEY_Home:
	case XKB_KEY_g:
		move = SWEETWALL_MOVE_FIRST;
		break;
	case XKB_KEY_End:
	case XKB_KEY_G:
		move = SWEETWALL_MOVE_LAST;
		break;
	default:
		return;
	}

	if (sweetwall_grid_move(
		    &app->grid, &app->layout, app->layer.height, move)) {
		app->layer.needs_repaint = true;
	}
}

static void handle_focus_lost(void *user_data) {
	struct sweetwall_app *app = user_data;
	app->running = false;
}

static const struct sweetwall_seat_handler seat_handler = {
	.key = handle_key,
	.focus_lost = handle_focus_lost,
};

// TODO: replace with the configured directory
static char *default_directory(void) {
	const char *home = getenv("HOME");
	if (home == NULL) {
		fprintf(stderr, "sweetwall: HOME is not set\n");
		return NULL;
	}

	size_t size = strlen(home) + 1 + strlen(DEFAULT_DIRECTORY) + 1;
	char *path = malloc(size);
	if (path != NULL) {
		snprintf(path, size, "%s/%s", home, DEFAULT_DIRECTORY);
	}
	return path;
}

bool sweetwall_app_init(struct sweetwall_app *app) {
	*app = (struct sweetwall_app){
		.layout = default_layout,
		.background = default_background,
		.tile = default_tile,
		.ring = default_ring,
		.running = true,
	};

	if (!install_signal_handlers()) {
		fprintf(stderr,
			"sweetwall: failed to install signal handlers\n");
		return false;
	}

	char *directory = default_directory();
	if (directory == NULL) {
		return false;
	}
	bool scanned = sweetwall_dirscan_run(&app->scan, directory);
	free(directory);
	if (!scanned) {
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

	if (!sweetwall_seat_init(
		    &app->seat, app->registry.seat, &seat_handler, app)) {
		return false;
	}

	uint32_t width;
	uint32_t height;
	sweetwall_layout_surface_size(&app->layout, app->scan.count,
		MAX_VISIBLE_ROWS, &width, &height);

	if (!sweetwall_layer_create(&app->layer, &app->registry, width, height,
		    DEFAULT_POSITION)) {
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

	sweetwall_grid_reveal(&app->grid, &app->layout, app->layer.height);

	int32_t step = (int32_t)(app->layout.tile_height + app->layout.spacing);

	struct sweetwall_frame frame = {
		.layout = &app->layout,
		.item_count = app->scan.count,
		.selected = app->grid.selected,
		.scroll = (int32_t)app->grid.first_row * step,
		.surface_width = app->layer.width,
		.surface_height = app->layer.height,
		.scale = scale,
		.background = sweetwall_color_argb(app->background),
		.tile = sweetwall_color_argb(app->tile),
		.ring = sweetwall_color_argb(app->ring),
	};
	sweetwall_frame_draw(buffer, &frame);
	sweetwall_layer_commit_frame(&app->layer);
	return true;
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

	struct pollfd pfd = {
		.fd = wl_display_get_fd(app->display),
		.events = POLLIN,
	};

	int timeout = sweetwall_seat_repeat_timeout(&app->seat);
	int ready = poll(&pfd, 1, timeout);

	if (ready < 0) {
		wl_display_cancel_read(app->display);
		// A caught signal is a normal wakeup, not a failure
		return errno == EINTR;
	}

	if (ready == 0) {
		wl_display_cancel_read(app->display);
		sweetwall_seat_dispatch_repeat(&app->seat);
		return true;
	}

	if ((pfd.revents & (POLLERR | POLLHUP)) != 0) {
		wl_display_cancel_read(app->display);
		fprintf(stderr, "sweetwall: compositor disconnected\n");
		return false;
	}

	if (wl_display_read_events(app->display) < 0) {
		return false;
	}
	if (wl_display_dispatch_pending(app->display) < 0) {
		return false;
	}

	sweetwall_seat_dispatch_repeat(&app->seat);
	return true;
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
	sweetwall_seat_finish(&app->seat);
	sweetwall_registry_finish(&app->registry);
	if (app->display != NULL) {
		wl_display_disconnect(app->display);
		app->display = NULL;
	}
	sweetwall_dirscan_finish(&app->scan);
	app->running = false;
}
