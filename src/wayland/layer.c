#include "wayland/layer.h"

#include <stdlib.h>
#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define LAYER_NAMESPACE "sweetwall"
// fractional-scale-v1 reports scale in 120ths of the logical size
#define FRACTIONAL_SCALE_DENOM 120

// --- buffer lifetime ---

static void free_buffer(struct sweetwall_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	sweetwall_buffer_destroy(buffer);
	free(buffer);
}

static void collect_retired(struct sweetwall_layer *layer) {
	struct sweetwall_buffer **cursor = &layer->retired;
	while (*cursor != NULL) {
		struct sweetwall_buffer *buffer = *cursor;
		if (!buffer->released) {
			cursor = &buffer->next;
			continue;
		}
		*cursor = buffer->next;
		free_buffer(buffer);
	}
}

static void retire_current(struct sweetwall_layer *layer) {
	collect_retired(layer);
	if (layer->buffer == NULL) {
		return;
	}
	if (layer->buffer->released) {
		free_buffer(layer->buffer);
	} else {
		layer->buffer->next = layer->retired;
		layer->retired = layer->buffer;
	}
	layer->buffer = NULL;
}

// --- protocol listeners ---

static void handle_configure(void *data,
	struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
	uint32_t width, uint32_t height) {
	struct sweetwall_layer *layer = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (width != layer->width || height != layer->height) {
		layer->needs_repaint = true;
	}
	layer->width = width;
	layer->height = height;
	layer->configured = true;
}

// The compositor withdrew the surface; flag it and let the app tear down
static void handle_closed(
	void *data, struct zwlr_layer_surface_v1 *layer_surface) {
	struct sweetwall_layer *layer = data;
	(void)layer_surface;

	layer->closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = handle_configure,
	.closed = handle_closed,
};

static void handle_preferred_scale(
	void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale) {
	struct sweetwall_layer *layer = data;
	(void)fractional;

	if (scale != layer->fractional_scale) {
		layer->fractional_scale = scale;
		layer->needs_repaint = true;
	}
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener =
	{
		.preferred_scale = handle_preferred_scale,
};

static void handle_surface_enter(
	void *data, struct wl_surface *surface, struct wl_output *output) {
	(void)data;
	(void)surface;
	(void)output;
}

static void handle_surface_leave(
	void *data, struct wl_surface *surface, struct wl_output *output) {
	(void)data;
	(void)surface;
	(void)output;
}

// Integer-scale fallback for compositors without fractional-scale-v1
static void handle_preferred_buffer_scale(
	void *data, struct wl_surface *surface, int32_t factor) {
	struct sweetwall_layer *layer = data;
	(void)surface;

	if (factor >= 1 && factor != layer->buffer_scale) {
		layer->buffer_scale = factor;
		layer->needs_repaint = true;
	}
}

static void handle_preferred_buffer_transform(
	void *data, struct wl_surface *surface, uint32_t transform) {
	(void)data;
	(void)surface;
	(void)transform;
}

static const struct wl_surface_listener surface_listener = {
	.enter = handle_surface_enter,
	.leave = handle_surface_leave,
	.preferred_buffer_scale = handle_preferred_buffer_scale,
	.preferred_buffer_transform = handle_preferred_buffer_transform,
};

// --- surface setup ---

static uint32_t anchor_for(enum sweetwall_position position) {
	switch (position) {
	case SWEETWALL_POSITION_LEFT:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	case SWEETWALL_POSITION_RIGHT:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	case SWEETWALL_POSITION_TOP:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	case SWEETWALL_POSITION_BOTTOM:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	case SWEETWALL_POSITION_CENTER:
		break;
	}
	// No anchor leaves the compositor to center the surface
	return 0;
}

bool sweetwall_layer_create(struct sweetwall_layer *layer,
	const struct sweetwall_registry *reg, uint32_t width, uint32_t height,
	enum sweetwall_position position) {
	*layer = (struct sweetwall_layer){
		.width = width,
		.height = height,
		.buffer_scale = 1,
	};

	layer->wl_surface = wl_compositor_create_surface(reg->compositor);
	if (layer->wl_surface == NULL) {
		return false;
	}
	wl_surface_add_listener(layer->wl_surface, &surface_listener, layer);

	if (reg->viewporter != NULL && reg->fractional_scale_manager != NULL) {
		layer->viewport = wp_viewporter_get_viewport(
			reg->viewporter, layer->wl_surface);
		layer->fractional =
			wp_fractional_scale_manager_v1_get_fractional_scale(
				reg->fractional_scale_manager,
				layer->wl_surface);
		if (layer->fractional != NULL) {
			wp_fractional_scale_v1_add_listener(layer->fractional,
				&fractional_scale_listener, layer);
		}
	}

	// NULL output lets the compositor place the surface on the active one
	layer->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		reg->layer_shell, layer->wl_surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, LAYER_NAMESPACE);
	if (layer->layer_surface == NULL) {
		wl_surface_destroy(layer->wl_surface);
		layer->wl_surface = NULL;
		return false;
	}

	zwlr_layer_surface_v1_add_listener(
		layer->layer_surface, &layer_surface_listener, layer);
	zwlr_layer_surface_v1_set_size(layer->layer_surface, width, height);
	zwlr_layer_surface_v1_set_anchor(
		layer->layer_surface, anchor_for(position));
	// A picker overlays the desktop; it must not reserve space
	zwlr_layer_surface_v1_set_exclusive_zone(layer->layer_surface, 0);
	// TODO: request exclusive keyboard focus once input lands

	wl_surface_commit(layer->wl_surface);
	return true;
}

void sweetwall_layer_buffer_size(const struct sweetwall_layer *layer,
	uint32_t *pixel_width, uint32_t *pixel_height) {
	if (layer->fractional_scale > 0) {
		*pixel_width =
			(uint32_t)(((uint64_t)layer->width *
						   layer->fractional_scale +
					   FRACTIONAL_SCALE_DENOM - 1) /
				   FRACTIONAL_SCALE_DENOM);
		*pixel_height =
			(uint32_t)(((uint64_t)layer->height *
						   layer->fractional_scale +
					   FRACTIONAL_SCALE_DENOM - 1) /
				   FRACTIONAL_SCALE_DENOM);
		return;
	}
	int32_t scale = layer->buffer_scale < 1 ? 1 : layer->buffer_scale;
	*pixel_width = layer->width * (uint32_t)scale;
	*pixel_height = layer->height * (uint32_t)scale;
}

static void present(struct sweetwall_layer *layer) {
	if (layer->viewport != NULL && layer->fractional_scale > 0) {
		// The viewport maps the scaled buffer back to the logical size
		wl_surface_set_buffer_scale(layer->wl_surface, 1);
		wp_viewport_set_destination(layer->viewport,
			(int32_t)layer->width, (int32_t)layer->height);
	} else {
		int32_t scale =
			layer->buffer_scale < 1 ? 1 : layer->buffer_scale;
		wl_surface_set_buffer_scale(layer->wl_surface, scale);
	}

	wl_surface_attach(layer->wl_surface, layer->buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(layer->wl_surface, 0, 0,
		(int32_t)layer->buffer->width, (int32_t)layer->buffer->height);
	wl_surface_commit(layer->wl_surface);

	layer->buffer->released = false;
	layer->needs_repaint = false;
}

bool sweetwall_layer_paint_color(
	struct sweetwall_layer *layer, struct wl_shm *shm, uint32_t color) {
	if (!layer->configured || layer->wl_surface == NULL) {
		return false;
	}

	uint32_t pixel_width;
	uint32_t pixel_height;
	sweetwall_layer_buffer_size(layer, &pixel_width, &pixel_height);

	// Reuse the buffer when the geometry matches and nobody else holds it
	bool reusable = layer->buffer != NULL && layer->buffer->released &&
			layer->buffer->width == pixel_width &&
			layer->buffer->height == pixel_height;

	if (!reusable) {
		struct sweetwall_buffer *buffer = calloc(1, sizeof(*buffer));
		if (buffer == NULL) {
			return false;
		}
		if (!sweetwall_buffer_create(
			    buffer, shm, pixel_width, pixel_height)) {
			free(buffer);
			return false;
		}
		retire_current(layer);
		layer->buffer = buffer;
	}

	sweetwall_buffer_fill(layer->buffer, color);
	present(layer);
	return true;
}

void sweetwall_layer_destroy(struct sweetwall_layer *layer) {
	if (layer->fractional != NULL) {
		wp_fractional_scale_v1_destroy(layer->fractional);
		layer->fractional = NULL;
	}
	if (layer->viewport != NULL) {
		wp_viewport_destroy(layer->viewport);
		layer->viewport = NULL;
	}
	if (layer->layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(layer->layer_surface);
		layer->layer_surface = NULL;
	}
	if (layer->wl_surface != NULL) {
		wl_surface_destroy(layer->wl_surface);
		layer->wl_surface = NULL;
	}

	// Free pixels only after the surface stops referencing them
	free_buffer(layer->buffer);
	layer->buffer = NULL;
	while (layer->retired != NULL) {
		struct sweetwall_buffer *buffer = layer->retired;
		layer->retired = buffer->next;
		free_buffer(buffer);
	}
	layer->configured = false;
}
