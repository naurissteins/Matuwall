#ifndef SWEETWALL_WAYLAND_POINTER_H
#define SWEETWALL_WAYLAND_POINTER_H

#include <stdint.h>

struct wl_pointer;
struct sweetwall_seat_handler;

struct sweetwall_pointer {
	struct wl_pointer *wl_pointer;
	int32_t x;
	int32_t y;
	double scroll_accum;

	const struct sweetwall_seat_handler *handler;
	void *user_data;
};

void sweetwall_pointer_init(struct sweetwall_pointer *pointer,
	struct wl_pointer *wl_pointer,
	const struct sweetwall_seat_handler *handler, void *user_data);

// Release the wl_pointer and reset state
void sweetwall_pointer_finish(struct sweetwall_pointer *pointer);

#endif
