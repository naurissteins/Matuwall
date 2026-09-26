#include "wayland/layer.h"

#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define PANEL_NAMESPACE "matuwall"
// its own namespace lets compositor rules treat the preview apart
#define BACKDROP_NAMESPACE "matuwall-preview"
// fractional-scale-v1 reports scale in 120ths of the logical size
#define FRACTIONAL_SCALE_DENOM 120

// --- protocol listeners ---

static void handle_configure(void *data,
	struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
	uint32_t width, uint32_t height) {
	struct matuwall_layer *layer = data;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

	if (width == 0) {
		width = layer->requested_width != 0 ? layer->requested_width
						    : layer->width;
	}
	if (height == 0) {
		height = layer->requested_height != 0 ? layer->requested_height
						      : layer->height;
	}
	if (width == 0 || height == 0) {
		layer->configured = false;
		return;
	}
	if (width != layer->width || height != layer->height) {
		layer->needs_repaint = true;
		layer->layout_dirty = true;
	}
	layer->width = width;
	layer->height = height;
	layer->configured = true;
}

// The compositor withdrew the surface; flag it and let the app tear down
static void handle_closed(
	void *data, struct zwlr_layer_surface_v1 *layer_surface) {
	struct matuwall_layer *layer = data;
	(void)layer_surface;

	layer->closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = handle_configure,
	.closed = handle_closed,
};

static void handle_preferred_scale(
	void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale) {
	struct matuwall_layer *layer = data;
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
	struct matuwall_layer *layer = data;
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

static uint32_t anchor_for(enum matuwall_position position) {
	switch (position) {
	case MATUWALL_POSITION_LEFT:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	case MATUWALL_POSITION_RIGHT:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	case MATUWALL_POSITION_TOP:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	case MATUWALL_POSITION_BOTTOM:
		return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	case MATUWALL_POSITION_CENTER:
		break;
	}
	// No anchor leaves the compositor to center the surface
	return 0;
}

// All four edges plus a zero size makes the compositor hand us the output size
#define ANCHOR_ALL                                                             \
	(ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |                                    \
		ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |                          \
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |                            \
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)

static void place_panel(struct matuwall_layer *layer,
	const struct matuwall_layer_panel *panel) {
	int32_t top = 0;
	int32_t right = 0;
	int32_t bottom = 0;
	int32_t left = 0;

	switch (panel->position) {
	case MATUWALL_POSITION_LEFT:
		left = (int32_t)panel->margin;
		break;
	case MATUWALL_POSITION_RIGHT:
		right = (int32_t)panel->margin;
		break;
	case MATUWALL_POSITION_TOP:
		top = (int32_t)panel->margin;
		break;
	case MATUWALL_POSITION_BOTTOM:
		bottom = (int32_t)panel->margin;
		break;
	case MATUWALL_POSITION_CENTER:
		break;
	}

	layer->requested_width = panel->width;
	layer->requested_height = panel->height;
	zwlr_layer_surface_v1_set_size(
		layer->layer_surface, panel->width, panel->height);
	zwlr_layer_surface_v1_set_anchor(
		layer->layer_surface, anchor_for(panel->position));
	zwlr_layer_surface_v1_set_margin(
		layer->layer_surface, top, right, bottom, left);
}

bool matuwall_layer_create(struct matuwall_layer *layer,
	const struct matuwall_registry *reg, struct wl_output *output,
	enum matuwall_layer_role role,
	const struct matuwall_layer_panel *panel) {
	*layer = (struct matuwall_layer){
		.compositor = reg->compositor,
		.buffer_scale = 1,
	};
	bool backdrop = role == MATUWALL_LAYER_BACKDROP;
	// the backdrop paints once per preview, a spare would only hold memory
	layer->keep_spare = !backdrop;

	layer->wl_surface = wl_compositor_create_surface(reg->compositor);
	if (layer->wl_surface == NULL) {
		return false;
	}
	wl_surface_add_listener(layer->wl_surface, &surface_listener, layer);

	// a viewport also lets the backdrop show a capped image at full size
	if (reg->viewporter != NULL &&
		(backdrop || reg->fractional_scale_manager != NULL)) {
		layer->viewport = wp_viewporter_get_viewport(
			reg->viewporter, layer->wl_surface);
	}
	if (layer->viewport != NULL && reg->fractional_scale_manager != NULL) {
		layer->fractional =
			wp_fractional_scale_manager_v1_get_fractional_scale(
				reg->fractional_scale_manager,
				layer->wl_surface);
		if (layer->fractional != NULL) {
			wp_fractional_scale_v1_add_listener(layer->fractional,
				&fractional_scale_listener, layer);
		}
	}

	// NULL keeps the compositor-selected active output behavior
	layer->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		reg->layer_shell, layer->wl_surface, output,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		backdrop ? BACKDROP_NAMESPACE : PANEL_NAMESPACE);
	if (layer->layer_surface == NULL) {
		wl_surface_destroy(layer->wl_surface);
		layer->wl_surface = NULL;
		return false;
	}

	zwlr_layer_surface_v1_add_listener(
		layer->layer_surface, &layer_surface_listener, layer);
	if (panel != NULL) {
		place_panel(layer, panel);
	} else {
		zwlr_layer_surface_v1_set_size(layer->layer_surface, 0, 0);
		zwlr_layer_surface_v1_set_anchor(
			layer->layer_surface, ANCHOR_ALL);
	}
	// A picker overlays the desktop; it must not reserve space
	zwlr_layer_surface_v1_set_exclusive_zone(layer->layer_surface, 0);
	// every key belongs to the panel while it is open
	zwlr_layer_surface_v1_set_keyboard_interactivity(layer->layer_surface,
		backdrop
			? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE
			: ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

	wl_surface_commit(layer->wl_surface);
	return true;
}

void matuwall_layer_set_panel(struct matuwall_layer *layer,
	const struct matuwall_layer_panel *panel) {
	place_panel(layer, panel);
	layer->configured = false;
	wl_surface_commit(layer->wl_surface);
}

bool matuwall_layer_map_clear(
	struct matuwall_layer *layer, struct wl_shm *shm) {
	if (!layer->configured || layer->width > INT32_MAX ||
		layer->height > INT32_MAX) {
		return false;
	}
	// without a viewport the only way to cover the surface is a full buffer
	if (layer->viewport == NULL) {
		struct matuwall_buffer *buffer;
		struct matuwall_damage damage = {.x1 = 1, .y1 = 1};
		return matuwall_layer_begin_frame(layer, shm, &buffer) ==
			       MATUWALL_BUFFER_READY &&
		       matuwall_layer_commit_frame(
			       layer, false, false, &damage);
	}

	layer->clear_buffer = matuwall_shm_clear_pixel(shm);
	if (layer->clear_buffer == NULL) {
		return false;
	}
	int32_t width = (int32_t)layer->width;
	int32_t height = (int32_t)layer->height;
	wl_surface_attach(layer->wl_surface, layer->clear_buffer, 0, 0);
	wp_viewport_set_destination(layer->viewport, width, height);
	wl_surface_damage_buffer(layer->wl_surface, 0, 0, 1, 1);
	wl_surface_commit(layer->wl_surface);

	// the first real frame resends only what differs from this
	layer->geometry_sent = true;
	layer->sent_scale = 1;
	layer->sent_width = width;
	layer->sent_height = height;
	return true;
}

void matuwall_layer_inherit_scale(
	struct matuwall_layer *layer, const struct matuwall_layer *from) {
	if (layer->fractional_scale == 0 && layer->fractional != NULL) {
		layer->fractional_scale = from->fractional_scale;
	}
	if (layer->buffer_scale == 1 && from->buffer_scale > 1) {
		layer->buffer_scale = from->buffer_scale;
	}
}

void matuwall_layer_buffer_size(const struct matuwall_layer *layer,
	uint32_t *pixel_width, uint32_t *pixel_height) {
	if (layer->fractional_scale > 0) {
		// fractional-scale-v1 rounds the scaled size halfway away from
		// 0
		*pixel_width =
			(uint32_t)(((uint64_t)layer->width *
						   layer->fractional_scale +
					   FRACTIONAL_SCALE_DENOM / 2) /
				   FRACTIONAL_SCALE_DENOM);
		*pixel_height =
			(uint32_t)(((uint64_t)layer->height *
						   layer->fractional_scale +
					   FRACTIONAL_SCALE_DENOM / 2) /
				   FRACTIONAL_SCALE_DENOM);
		return;
	}
	int32_t scale = layer->buffer_scale < 1 ? 1 : layer->buffer_scale;
	*pixel_width = layer->width * (uint32_t)scale;
	*pixel_height = layer->height * (uint32_t)scale;
}

void matuwall_layer_destroy(struct matuwall_layer *layer) {
	if (layer->frame_callback != NULL) {
		wl_callback_destroy(layer->frame_callback);
		layer->frame_callback = NULL;
	}
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

	// The surface no longer references client-side buffer objects
	if (layer->clear_buffer != NULL) {
		wl_buffer_destroy(layer->clear_buffer);
		layer->clear_buffer = NULL;
	}
	matuwall_buffer_pool_destroy(&layer->buffer_pool);
	layer->compositor = NULL;
	layer->configured = false;
	layer->opaque = false;
}
