#ifndef SWEETWALL_WAYLAND_SEAT_H
#define SWEETWALL_WAYLAND_SEAT_H

#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

struct wl_seat;
struct wl_keyboard;

typedef void (*sweetwall_key_fn)(void *user_data, xkb_keysym_t sym);

struct sweetwall_seat {
	// Borrowed from the registry; released there
	struct wl_seat *wl_seat;
	struct wl_keyboard *keyboard;

	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_state *state;

	sweetwall_key_fn on_key;
	void *user_data;
};

bool sweetwall_seat_init(struct sweetwall_seat *seat, struct wl_seat *wl_seat,
	sweetwall_key_fn on_key, void *user_data);

void sweetwall_seat_finish(struct sweetwall_seat *seat);

#endif
