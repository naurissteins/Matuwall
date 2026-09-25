#include "wayland/seat.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "util/clock.h"
#include "util/log.h"

// evdev keycodes are offset by 8 in the xkb keymap
#define XKB_KEYCODE_OFFSET 8
#define MS_PER_SECOND 1000

static void stop_repeat(struct matuwall_seat *seat) {
	seat->repeat_key = 0;
	seat->repeat_sym = XKB_KEY_NoSymbol;
	seat->repeat_at_ms = 0;
}

static void clear_keymap(struct matuwall_seat *seat) {
	if (seat->state != NULL) {
		xkb_state_unref(seat->state);
		seat->state = NULL;
	}
	if (seat->keymap != NULL) {
		xkb_keymap_unref(seat->keymap);
		seat->keymap = NULL;
	}
}

static void drop_pending_keymap(struct matuwall_seat *seat) {
	if (seat->keymap_fd >= 0) {
		close(seat->keymap_fd);
		seat->keymap_fd = -1;
	}
}

// consumes fd, the current keymap stays in place if the new one fails
static void compile_keymap(struct matuwall_seat *seat, int fd, uint32_t size) {
	char *text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED) {
		matuwall_log_error(
			"input", "failed to map the keyboard keymap");
		close(fd);
		return;
	}

	struct xkb_keymap *keymap = xkb_keymap_new_from_string(seat->context,
		text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(text, size);
	close(fd);

	if (keymap == NULL) {
		matuwall_log_error(
			"input", "failed to compile the keyboard keymap");
		return;
	}

	struct xkb_state *state = xkb_state_new(keymap);
	if (state == NULL) {
		xkb_keymap_unref(keymap);
		matuwall_log_error(
			"input", "failed to create the keyboard state");
		return;
	}

	// Swap only once the replacement is fully built
	clear_keymap(seat);
	seat->keymap = keymap;
	seat->state = state;
}

// focus events need a mapped surface, so this runs after the first frame
static void load_pending_keymap(struct matuwall_seat *seat) {
	if (seat->keymap_fd < 0) {
		return;
	}
	int fd = seat->keymap_fd;
	seat->keymap_fd = -1;
	compile_keymap(seat, fd, seat->keymap_size);
}

// compact mode receives the keymap before the first frame, keep only the fd
static void handle_keymap(void *data, struct wl_keyboard *keyboard,
	uint32_t format, int32_t fd, uint32_t size) {
	struct matuwall_seat *seat = data;
	(void)keyboard;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		matuwall_log_warn(
			"input", "compositor sent an unsupported keymap");
		close(fd);
		return;
	}

	drop_pending_keymap(seat);
	seat->keymap_fd = fd;
	seat->keymap_size = size;
}

static void handle_key(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	struct matuwall_seat *seat = data;
	(void)keyboard;
	(void)serial;
	(void)time;

	load_pending_keymap(seat);
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
		seat->repeat_at_ms = matuwall_now_ms() + seat->repeat_delay;
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
	struct matuwall_seat *seat = data;
	(void)keyboard;
	(void)serial;

	load_pending_keymap(seat);
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
	struct matuwall_seat *seat = data;
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
	struct matuwall_seat *seat = data;
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

static void release_keyboard(struct matuwall_seat *seat) {
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
	struct matuwall_seat *seat = data;
	bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

	if (has_keyboard && seat->keyboard == NULL) {
		seat->keyboard = wl_seat_get_keyboard(wl_seat);
		if (seat->keyboard != NULL) {
			wl_keyboard_add_listener(
				seat->keyboard, &keyboard_listener, seat);
		}
	} else if (!has_keyboard && seat->keyboard != NULL) {
		release_keyboard(seat);
		drop_pending_keymap(seat);
		clear_keymap(seat);
		stop_repeat(seat);
	}

	bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
	bool wants_pointer = seat->handler.pointer_motion != NULL ||
			     seat->handler.pointer_button != NULL ||
			     seat->handler.pointer_scroll != NULL;
	if (has_pointer && wants_pointer && seat->pointer.wl_pointer == NULL) {
		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);
		if (wl_pointer != NULL) {
			matuwall_pointer_init(&seat->pointer, wl_pointer,
				&seat->handler, seat->user_data);
		}
	} else if ((!has_pointer || !wants_pointer) &&
		   seat->pointer.wl_pointer != NULL) {
		matuwall_pointer_finish(&seat->pointer);
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

bool matuwall_seat_init(struct matuwall_seat *seat, struct wl_seat *wl_seat,
	const struct matuwall_seat_handler *handler, void *user_data) {
	*seat = (struct matuwall_seat){
		.wl_seat = wl_seat,
		.handler = *handler,
		.user_data = user_data,
		.keymap_fd = -1,
	};

	seat->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (seat->context == NULL) {
		matuwall_log_error("input", "failed to create an xkb context");
		return false;
	}

	wl_seat_add_listener(wl_seat, &seat_listener, seat);
	return true;
}

int matuwall_seat_repeat_timeout(const struct matuwall_seat *seat) {
	if (seat->repeat_key == 0 || seat->repeat_rate <= 0) {
		return -1;
	}
	int64_t remaining = seat->repeat_at_ms - matuwall_now_ms();
	return remaining < 0 ? 0 : (int)remaining;
}

void matuwall_seat_dispatch_repeat(struct matuwall_seat *seat) {
	if (seat->repeat_key == 0 || seat->repeat_rate <= 0) {
		return;
	}
	int64_t now = matuwall_now_ms();
	if (now < seat->repeat_at_ms) {
		return;
	}

	int interval = MS_PER_SECOND / seat->repeat_rate;
	if (interval < 1) {
		interval = 1;
	}
	seat->repeat_at_ms += interval;
	// Discard missed repeats instead of replaying a burst after a stall
	if (seat->repeat_at_ms <= now) {
		seat->repeat_at_ms = now + interval;
	}
	if (seat->handler.key != NULL) {
		seat->handler.key(seat->user_data, seat->repeat_sym);
	}
}

void matuwall_seat_finish(struct matuwall_seat *seat) {
	stop_repeat(seat);
	matuwall_pointer_finish(&seat->pointer);
	release_keyboard(seat);
	drop_pending_keymap(seat);
	clear_keymap(seat);
	if (seat->context != NULL) {
		xkb_context_unref(seat->context);
		seat->context = NULL;
	}
}
