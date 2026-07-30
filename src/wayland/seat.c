#include "wayland/seat.h"

#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

// evdev keycodes are offset by 8 in the xkb keymap
#define XKB_KEYCODE_OFFSET 8
#define MS_PER_SECOND 1000

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * MS_PER_SECOND + ts.tv_nsec / 1000000;
}

static void stop_repeat(struct sweetwall_seat *seat) {
	seat->repeat_key = 0;
	seat->repeat_sym = XKB_KEY_NoSymbol;
	seat->repeat_at_ms = 0;
}

static void clear_keymap(struct sweetwall_seat *seat) {
	if (seat->state != NULL) {
		xkb_state_unref(seat->state);
		seat->state = NULL;
	}
	if (seat->keymap != NULL) {
		xkb_keymap_unref(seat->keymap);
		seat->keymap = NULL;
	}
}

static void handle_keymap(void *data, struct wl_keyboard *keyboard,
	uint32_t format, int32_t fd, uint32_t size) {
	struct sweetwall_seat *seat = data;
	(void)keyboard;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	char *text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED) {
		close(fd);
		return;
	}

	struct xkb_keymap *keymap = xkb_keymap_new_from_string(seat->context,
		text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(text, size);
	close(fd);

	if (keymap == NULL) {
		fprintf(stderr, "sweetwall: failed to compile the keymap\n");
		return;
	}

	struct xkb_state *state = xkb_state_new(keymap);
	if (state == NULL) {
		xkb_keymap_unref(keymap);
		fprintf(stderr, "sweetwall: failed to create the keyboard "
				"state\n");
		return;
	}

	// Swap only once the replacement is fully built
	clear_keymap(seat);
	seat->keymap = keymap;
	seat->state = state;
}

static void handle_key(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	struct sweetwall_seat *seat = data;
	(void)keyboard;
	(void)serial;
	(void)time;

	if (seat->state == NULL) {
		return;
	}

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (seat->repeat_key == key) {
			stop_repeat(seat);
		}
		return;
	}

	xkb_keycode_t code = key + XKB_KEYCODE_OFFSET;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(seat->state, code);
	if (sym == XKB_KEY_NoSymbol) {
		return;
	}

	// A newly pressed key takes over the repeat slot
	if (seat->repeat_rate > 0 && seat->keymap != NULL &&
		xkb_keymap_key_repeats(seat->keymap, code)) {
		seat->repeat_key = key;
		seat->repeat_sym = sym;
		seat->repeat_at_ms = now_ms() + seat->repeat_delay;
	} else {
		stop_repeat(seat);
	}

	if (seat->handler.key != NULL) {
		seat->handler.key(seat->user_data, sym);
	}
}

static void handle_modifiers(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
	uint32_t group) {
	struct sweetwall_seat *seat = data;
	(void)keyboard;
	(void)serial;

	if (seat->state != NULL) {
		xkb_state_update_mask(
			seat->state, depressed, latched, locked, 0, 0, group);
	}
}

static void handle_enter(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void handle_leave(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, struct wl_surface *surface) {
	struct sweetwall_seat *seat = data;
	(void)keyboard;
	(void)serial;
	(void)surface;

	// Nothing is held once focus is gone
	stop_repeat(seat);
	if (seat->handler.focus_lost != NULL) {
		seat->handler.focus_lost(seat->user_data);
	}
}

static void handle_repeat_info(
	void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
	struct sweetwall_seat *seat = data;
	(void)keyboard;

	seat->repeat_rate = rate < 0 ? 0 : rate;
	seat->repeat_delay = delay < 0 ? 0 : delay;
	if (seat->repeat_rate == 0) {
		stop_repeat(seat);
	}
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = handle_keymap,
	.enter = handle_enter,
	.leave = handle_leave,
	.key = handle_key,
	.modifiers = handle_modifiers,
	.repeat_info = handle_repeat_info,
};

static void release_keyboard(struct sweetwall_seat *seat) {
	if (seat->keyboard == NULL) {
		return;
	}
	if (wl_keyboard_get_version(seat->keyboard) >=
		WL_KEYBOARD_RELEASE_SINCE_VERSION) {
		wl_keyboard_release(seat->keyboard);
	} else {
		wl_keyboard_destroy(seat->keyboard);
	}
	seat->keyboard = NULL;
}

// Keyboards come and go with the seat's capabilities, not just at startup
static void handle_capabilities(
	void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	struct sweetwall_seat *seat = data;
	bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

	if (has_keyboard && seat->keyboard == NULL) {
		seat->keyboard = wl_seat_get_keyboard(wl_seat);
		if (seat->keyboard != NULL) {
			wl_keyboard_add_listener(
				seat->keyboard, &keyboard_listener, seat);
		}
	} else if (!has_keyboard && seat->keyboard != NULL) {
		release_keyboard(seat);
		clear_keymap(seat);
		stop_repeat(seat);
	}

	bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
	if (has_pointer && seat->pointer.wl_pointer == NULL) {
		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);
		if (wl_pointer != NULL) {
			sweetwall_pointer_init(&seat->pointer, wl_pointer,
				&seat->handler, seat->user_data);
		}
	} else if (!has_pointer && seat->pointer.wl_pointer != NULL) {
		sweetwall_pointer_finish(&seat->pointer);
	}
}

static void handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
	(void)data;
	(void)wl_seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = handle_capabilities,
	.name = handle_name,
};

bool sweetwall_seat_init(struct sweetwall_seat *seat, struct wl_seat *wl_seat,
	const struct sweetwall_seat_handler *handler, void *user_data) {
	*seat = (struct sweetwall_seat){
		.wl_seat = wl_seat,
		.handler = *handler,
		.user_data = user_data,
	};

	seat->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (seat->context == NULL) {
		fprintf(stderr, "sweetwall: failed to create an xkb context\n");
		return false;
	}

	wl_seat_add_listener(wl_seat, &seat_listener, seat);
	return true;
}

int sweetwall_seat_repeat_timeout(const struct sweetwall_seat *seat) {
	if (seat->repeat_key == 0 || seat->repeat_rate <= 0) {
		return -1;
	}
	int64_t remaining = seat->repeat_at_ms - now_ms();
	return remaining < 0 ? 0 : (int)remaining;
}

void sweetwall_seat_dispatch_repeat(struct sweetwall_seat *seat) {
	if (seat->repeat_key == 0 || seat->repeat_rate <= 0) {
		return;
	}
	if (now_ms() < seat->repeat_at_ms) {
		return;
	}

	seat->repeat_at_ms += MS_PER_SECOND / seat->repeat_rate;
	if (seat->handler.key != NULL) {
		seat->handler.key(seat->user_data, seat->repeat_sym);
	}
}

void sweetwall_seat_finish(struct sweetwall_seat *seat) {
	stop_repeat(seat);
	sweetwall_pointer_finish(&seat->pointer);
	release_keyboard(seat);
	clear_keymap(seat);
	if (seat->context != NULL) {
		xkb_context_unref(seat->context);
		seat->context = NULL;
	}
}
