#ifndef SWEETWALL_WAYLAND_LAYER_H
#define SWEETWALL_WAYLAND_LAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "grid/layout.h"
#include "wayland/registry.h"
#include "wayland/shm.h"

struct zwlr_layer_surface_v1;
struct wp_viewport;
struct wp_fractional_scale_v1;

struct sweetwall_layer {
	struct wl_surface *wl_surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;

	uint32_t width;
	uint32_t height;
	uint32_t fractional_scale;
	int32_t buffer_scale;

	bool configured;
	bool needs_repaint;
	bool closed;

	struct sweetwall_buffer *buffer;
	struct sweetwall_buffer *retired;
};

// Starts bufferless across the selected output so its bounds are known
bool sweetwall_layer_create(
	struct sweetwall_layer *layer, const struct sweetwall_registry *reg);

// Replace the output probe with the final compact panel geometry
void sweetwall_layer_set_panel(struct sweetwall_layer *layer, uint32_t width,
	uint32_t height, enum sweetwall_position position);

void sweetwall_layer_buffer_size(const struct sweetwall_layer *layer,
	uint32_t *pixel_width, uint32_t *pixel_height);

struct sweetwall_buffer *sweetwall_layer_begin_frame(
	struct sweetwall_layer *layer, struct wl_shm *shm);

void sweetwall_layer_commit_frame(struct sweetwall_layer *layer);

void sweetwall_layer_destroy(struct sweetwall_layer *layer);

#endif
