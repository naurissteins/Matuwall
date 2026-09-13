#ifndef MATUWALL_WAYLAND_SEAT_H
#define MATUWALL_WAYLAND_SEAT_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include "wayland/pointer.h"

struct wl_seat;
struct wl_keyboard;

struct matuwall_seat_handler {
	void (*key)(void *user_data, xkb_keysym_t sym);
	void (*focus_lost)(void *user_data);
	// Pointer coordinates are surface-local logical pixels
	void (*pointer_motion)(void *user_data, int32_t x, int32_t y);
	void (*pointer_button)(
		void *user_data, int32_t x, int32_t y, bool pressed);
	// steps is signed: positive scrolls down, negative up
	void (*pointer_scroll)(void *user_data, int32_t steps);
};

struct matuwall_seat {
	struct wl_seat *wl_seat;
	struct wl_keyboard *keyboard;
	struct matuwall_pointer pointer;

	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_state *state;

	int32_t repeat_rate;
	int32_t repeat_delay;
	uint32_t repeat_key;
	xkb_keysym_t repeat_sym;
	int64_t repeat_at_ms;

	struct matuwall_seat_handler handler;
	void *user_data;
};

bool matuwall_seat_init(struct matuwall_seat *seat, struct wl_seat *wl_seat,
	const struct matuwall_seat_handler *handler, void *user_data);

int matuwall_seat_repeat_timeout(const struct matuwall_seat *seat);

void matuwall_seat_dispatch_repeat(struct matuwall_seat *seat);

void matuwall_seat_finish(struct matuwall_seat *seat);

#endif
