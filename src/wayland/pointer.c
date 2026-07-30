#include "wayland/pointer.h"

#include <wayland-client.h>

#include "wayland/seat.h"

// evdev left mouse button
#define BTN_LEFT 0x110
// One wheel notch of accumulated axis value maps to one scroll step
#define SCROLL_NOTCH 10.0

static void handle_enter(void *data, struct wl_pointer *wl_pointer,
	uint32_t serial, struct wl_surface *surface, wl_fixed_t x,
	wl_fixed_t y) {
	struct sweetwall_pointer *pointer = data;
	(void)wl_pointer;
	(void)serial;
	(void)surface;

	// Treat entering as a move so a tile highlights without a jitter first
	pointer->x = wl_fixed_to_int(x);
	pointer->y = wl_fixed_to_int(y);
	if (pointer->handler->pointer_motion != NULL) {
		pointer->handler->pointer_motion(
			pointer->user_data, pointer->x, pointer->y);
	}
}

static void handle_leave(void *data, struct wl_pointer *wl_pointer,
	uint32_t serial, struct wl_surface *surface) {
	(void)data;
	(void)wl_pointer;
	(void)serial;
	(void)surface;
}

static void handle_motion(void *data, struct wl_pointer *wl_pointer,
	uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	struct sweetwall_pointer *pointer = data;
	(void)wl_pointer;
	(void)time;

	pointer->x = wl_fixed_to_int(x);
	pointer->y = wl_fixed_to_int(y);
	if (pointer->handler->pointer_motion != NULL) {
		pointer->handler->pointer_motion(
			pointer->user_data, pointer->x, pointer->y);
	}
}

static void handle_button(void *data, struct wl_pointer *wl_pointer,
	uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct sweetwall_pointer *pointer = data;
	(void)wl_pointer;
	(void)serial;
	(void)time;

	if (button != BTN_LEFT || pointer->handler->pointer_button == NULL) {
		return;
	}
	bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
	pointer->handler->pointer_button(
		pointer->user_data, pointer->x, pointer->y, pressed);
}

static void handle_axis(void *data, struct wl_pointer *wl_pointer,
	uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct sweetwall_pointer *pointer = data;
	(void)wl_pointer;
	(void)time;

	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		pointer->scroll_accum += wl_fixed_to_double(value);
	}
}

// Emit whole scroll steps once the axis group for this frame is complete
static void handle_frame(void *data, struct wl_pointer *wl_pointer) {
	struct sweetwall_pointer *pointer = data;
	(void)wl_pointer;

	int32_t steps = (int32_t)(pointer->scroll_accum / SCROLL_NOTCH);
	if (steps != 0) {
		pointer->scroll_accum -= steps * SCROLL_NOTCH;
		if (pointer->handler->pointer_scroll != NULL) {
			pointer->handler->pointer_scroll(
				pointer->user_data, steps);
		}
	}
}

static void handle_axis_source(
	void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
	(void)data;
	(void)wl_pointer;
	(void)axis_source;
}

static void handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
	uint32_t time, uint32_t axis) {
	(void)data;
	(void)wl_pointer;
	(void)time;
	(void)axis;
}

static void handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
	uint32_t axis, int32_t discrete) {
	(void)data;
	(void)wl_pointer;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = handle_enter,
	.leave = handle_leave,
	.motion = handle_motion,
	.button = handle_button,
	.axis = handle_axis,
	.frame = handle_frame,
	.axis_source = handle_axis_source,
	.axis_stop = handle_axis_stop,
	.axis_discrete = handle_axis_discrete,
};

void sweetwall_pointer_init(struct sweetwall_pointer *pointer,
	struct wl_pointer *wl_pointer,
	const struct sweetwall_seat_handler *handler, void *user_data) {
	*pointer = (struct sweetwall_pointer){
		.wl_pointer = wl_pointer,
		.handler = handler,
		.user_data = user_data,
	};
	wl_pointer_add_listener(wl_pointer, &pointer_listener, pointer);
}

void sweetwall_pointer_finish(struct sweetwall_pointer *pointer) {
	if (pointer->wl_pointer == NULL) {
		return;
	}
	if (wl_pointer_get_version(pointer->wl_pointer) >=
		WL_POINTER_RELEASE_SINCE_VERSION) {
		wl_pointer_release(pointer->wl_pointer);
	} else {
		wl_pointer_destroy(pointer->wl_pointer);
	}
	pointer->wl_pointer = NULL;
	pointer->scroll_accum = 0.0;
}
