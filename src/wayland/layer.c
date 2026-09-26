#include "wayland/layer.h"

#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "util/clock.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define LAYER_NAMESPACE "matuwall"
// fractional-scale-v1 reports scale in 120ths of the logical size
#define FRACTIONAL_SCALE_DENOM 120
// compositors that hold the attached buffer would otherwise force a fresh
// output-sized buffer and a full redraw for every sporadic repaint
#define IDLE_COLLECT_MS 2000

// --- frame pacing ---

static bool collect_blocked(const struct matuwall_layer *layer) {
	return layer->collect_due_ms == 0 || layer->needs_repaint ||
	       layer->frame_callback != NULL ||
	       layer->buffer_pool.drawing != NULL || !layer->configured;
}

int matuwall_layer_idle_timeout(
	const struct matuwall_layer *layer, int64_t now_ms) {
	if (collect_blocked(layer)) {
		return -1;
	}
	int64_t left = layer->collect_due_ms - now_ms;
	return left > 0 ? (int)left : 0;
}

void matuwall_layer_collect_idle(struct matuwall_layer *layer, int64_t now_ms) {
	if (collect_blocked(layer) || now_ms < layer->collect_due_ms) {
		return;
	}
	layer->collect_due_ms = 0;

	uint32_t width;
	uint32_t height;
	matuwall_layer_buffer_size(layer, &width, &height);
	matuwall_buffer_pool_collect_idle(&layer->buffer_pool, width, height);
}

static void handle_frame_done(
	void *data, struct wl_callback *callback, uint32_t time) {
	struct matuwall_layer *layer = data;
	(void)time;

	wl_callback_destroy(callback);
	layer->frame_callback = NULL;
}

static const struct wl_callback_listener frame_listener = {
	.done = handle_frame_done,
};

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

bool matuwall_layer_create(struct matuwall_layer *layer,
	const struct matuwall_registry *reg, struct wl_output *output) {
	*layer = (struct matuwall_layer){
		.compositor = reg->compositor,
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

	// NULL keeps the compositor-selected active output behavior
	layer->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		reg->layer_shell, layer->wl_surface, output,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, LAYER_NAMESPACE);
	if (layer->layer_surface == NULL) {
		wl_surface_destroy(layer->wl_surface);
		layer->wl_surface = NULL;
		return false;
	}

	zwlr_layer_surface_v1_add_listener(
		layer->layer_surface, &layer_surface_listener, layer);
	zwlr_layer_surface_v1_set_size(layer->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_anchor(layer->layer_surface, ANCHOR_ALL);
	// A picker overlays the desktop; it must not reserve space
	zwlr_layer_surface_v1_set_exclusive_zone(layer->layer_surface, 0);
	// Every key belongs to the picker while it is open
	zwlr_layer_surface_v1_set_keyboard_interactivity(layer->layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

	wl_surface_commit(layer->wl_surface);
	return true;
}

void matuwall_layer_set_panel(struct matuwall_layer *layer, uint32_t width,
	uint32_t height, enum matuwall_position position, uint32_t margin) {
	int32_t top = 0;
	int32_t right = 0;
	int32_t bottom = 0;
	int32_t left = 0;

	switch (position) {
	case MATUWALL_POSITION_LEFT:
		left = (int32_t)margin;
		break;
	case MATUWALL_POSITION_RIGHT:
		right = (int32_t)margin;
		break;
	case MATUWALL_POSITION_TOP:
		top = (int32_t)margin;
		break;
	case MATUWALL_POSITION_BOTTOM:
		bottom = (int32_t)margin;
		break;
	case MATUWALL_POSITION_CENTER:
		break;
	}

	layer->requested_width = width;
	layer->requested_height = height;
	layer->configured = false;
	zwlr_layer_surface_v1_set_size(layer->layer_surface, width, height);
	zwlr_layer_surface_v1_set_anchor(
		layer->layer_surface, anchor_for(position));
	zwlr_layer_surface_v1_set_margin(
		layer->layer_surface, top, right, bottom, left);
	wl_surface_commit(layer->wl_surface);
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

static bool update_opaque_region(struct matuwall_layer *layer, bool opaque) {
	if (!opaque) {
		if (layer->opaque) {
			wl_surface_set_opaque_region(layer->wl_surface, NULL);
			layer->opaque = false;
			layer->opaque_width = 0;
			layer->opaque_height = 0;
		}
		return true;
	}
	if (layer->opaque && layer->opaque_width == layer->width &&
		layer->opaque_height == layer->height) {
		return true;
	}
	if (layer->compositor == NULL || layer->width == 0 ||
		layer->height == 0 || layer->width > INT32_MAX ||
		layer->height > INT32_MAX) {
		return false;
	}

	struct wl_region *region =
		wl_compositor_create_region(layer->compositor);
	if (region == NULL) {
		return false;
	}
	wl_region_add(
		region, 0, 0, (int32_t)layer->width, (int32_t)layer->height);
	wl_surface_set_opaque_region(layer->wl_surface, region);
	wl_region_destroy(region);
	layer->opaque = true;
	layer->opaque_width = layer->width;
	layer->opaque_height = layer->height;
	return true;
}

// surface state persists across commits; resend only what changed
static void send_geometry(struct matuwall_layer *layer) {
	bool viewport = layer->viewport != NULL && layer->fractional_scale > 0;
	int32_t scale = 1;
	int32_t width = -1;
	int32_t height = -1;
	if (viewport) {
		// viewport maps the scaled buffer back to the logical size
		width = (int32_t)layer->width;
		height = (int32_t)layer->height;
	} else if (layer->buffer_scale > 1) {
		scale = layer->buffer_scale;
	}
	if (layer->geometry_sent && layer->sent_scale == scale &&
		layer->sent_width == width && layer->sent_height == height) {
		return;
	}
	wl_surface_set_buffer_scale(layer->wl_surface, scale);
	if (viewport) {
		wp_viewport_set_destination(layer->viewport, width, height);
	} else if (layer->viewport != NULL && layer->geometry_sent &&
		   layer->sent_width >= 0) {
		// -1, -1 unsets a destination left over from fractional scale
		wp_viewport_set_destination(layer->viewport, -1, -1);
	}
	layer->geometry_sent = true;
	layer->sent_scale = scale;
	layer->sent_width = width;
	layer->sent_height = height;
}

static bool present(struct matuwall_layer *layer, bool continue_frames,
	bool opaque, const struct matuwall_damage *damage) {
	struct matuwall_buffer *buffer = layer->buffer_pool.drawing;
	if (damage == NULL || damage->x0 < 0 || damage->y0 < 0 ||
		damage->x1 > (int32_t)buffer->width ||
		damage->y1 > (int32_t)buffer->height ||
		damage->x0 >= damage->x1 || damage->y0 >= damage->y1) {
		return false;
	}
	if (!update_opaque_region(layer, opaque)) {
		return false;
	}
	if (layer->frame_callback == NULL) {
		layer->frame_callback = wl_surface_frame(layer->wl_surface);
		if (layer->frame_callback == NULL) {
			return false;
		}
		// without done event frame pacing would never reopen
		if (wl_callback_add_listener(layer->frame_callback,
			    &frame_listener, layer) < 0) {
			wl_callback_destroy(layer->frame_callback);
			layer->frame_callback = NULL;
			return false;
		}
	}

	send_geometry(layer);

	wl_surface_attach(layer->wl_surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage_buffer(layer->wl_surface, damage->x0, damage->y0,
		damage->x1 - damage->x0, damage->y1 - damage->y0);
	wl_surface_commit(layer->wl_surface);

	matuwall_buffer_pool_submitted(&layer->buffer_pool);
	layer->needs_repaint = continue_frames;
	layer->collect_due_ms = matuwall_now_ms() + IDLE_COLLECT_MS;
	return true;
}

enum matuwall_buffer_acquire matuwall_layer_begin_frame(
	struct matuwall_layer *layer, struct wl_shm *shm,
	struct matuwall_buffer **out) {
	*out = NULL;
	if (!layer->configured || layer->wl_surface == NULL) {
		return MATUWALL_BUFFER_FAILED;
	}
	// A requested callback is the compositor's permission for the next
	// frame
	if (layer->frame_callback != NULL) {
		return MATUWALL_BUFFER_BUSY;
	}

	uint32_t pixel_width;
	uint32_t pixel_height;
	matuwall_layer_buffer_size(layer, &pixel_width, &pixel_height);

	return matuwall_buffer_pool_acquire(
		&layer->buffer_pool, shm, pixel_width, pixel_height, out);
}

bool matuwall_layer_commit_frame(struct matuwall_layer *layer,
	bool continue_frames, bool opaque,
	const struct matuwall_damage *damage) {
	if (layer->buffer_pool.drawing == NULL) {
		return false;
	}
	return present(layer, continue_frames, opaque, damage);
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
	matuwall_buffer_pool_destroy(&layer->buffer_pool);
	layer->compositor = NULL;
	layer->configured = false;
	layer->opaque = false;
}
