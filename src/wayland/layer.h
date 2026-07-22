#ifndef SWEETWALL_WAYLAND_LAYER_H
#define SWEETWALL_WAYLAND_LAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "wayland/registry.h"
#include "wayland/shm.h"

struct zwlr_layer_surface_v1;
struct wp_viewport;
struct wp_fractional_scale_v1;

// Where the picker sits on screen
enum sweetwall_position {
	SWEETWALL_POSITION_CENTER,
	SWEETWALL_POSITION_LEFT,
	SWEETWALL_POSITION_RIGHT,
	SWEETWALL_POSITION_TOP,
	SWEETWALL_POSITION_BOTTOM,
};

struct sweetwall_layer {
	struct wl_surface *wl_surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;

	// Logical (surface-local) size, set by the compositor's configure
	uint32_t width;
	uint32_t height;
	// Preferred scale in 120ths; 0 when fractional-scale-v1 is unavailable
	uint32_t fractional_scale;
	// Integer fallback from wl_surface.preferred_buffer_scale, >= 1
	int32_t buffer_scale;

	bool configured;
	bool needs_repaint;
	// The compositor withdrew the surface; the app must exit
	bool closed;

	struct sweetwall_buffer *buffer;
	// Buffers the compositor still holds; freed once released
	struct sweetwall_buffer *retired;
};

bool sweetwall_layer_create(struct sweetwall_layer *layer,
	const struct sweetwall_registry *reg, uint32_t width, uint32_t height,
	enum sweetwall_position position);

// Resolve the logical size to physical pixels at the current scale
void sweetwall_layer_buffer_size(const struct sweetwall_layer *layer,
	uint32_t *pixel_width, uint32_t *pixel_height);

// Paint a flat premultiplied ARGB8888 color and commit
bool sweetwall_layer_paint_color(
	struct sweetwall_layer *layer, struct wl_shm *shm, uint32_t color);

void sweetwall_layer_destroy(struct sweetwall_layer *layer);

#endif
