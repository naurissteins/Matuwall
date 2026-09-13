#ifndef MATUWALL_WAYLAND_POINTER_H
#define MATUWALL_WAYLAND_POINTER_H

#include <stdint.h>

struct wl_pointer;
struct matuwall_seat_handler;

struct matuwall_pointer {
	struct wl_pointer *wl_pointer;
	int32_t x;
	int32_t y;
	double scroll_accum;

	const struct matuwall_seat_handler *handler;
	void *user_data;
};

void matuwall_pointer_init(struct matuwall_pointer *pointer,
	struct wl_pointer *wl_pointer,
	const struct matuwall_seat_handler *handler, void *user_data);

// Release the wl_pointer and reset state
void matuwall_pointer_finish(struct matuwall_pointer *pointer);

#endif
