#include "wayland/layer.h"

#include <wayland-client.h>

#include "util/clock.h"
#include "viewporter-client-protocol.h"

// compositors that hold the attached buffer would otherwise force a fresh
// output-sized buffer and a full redraw for every sporadic repaint
#define IDLE_COLLECT_MS 2000

// --- idle collection ---

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

	// a zero size matches no buffer, so every released one is freed
	uint32_t width = 0;
	uint32_t height = 0;
	if (layer->keep_spare) {
		matuwall_layer_buffer_size(layer, &width, &height);
	}
	matuwall_buffer_pool_collect_idle(&layer->buffer_pool, width, height);
}

// --- frame submission ---

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
static void send_geometry(
	struct matuwall_layer *layer, const struct matuwall_buffer *buffer) {
	uint32_t native_width;
	uint32_t native_height;
	matuwall_layer_buffer_size(layer, &native_width, &native_height);
	bool native = buffer->width == native_width &&
		      buffer->height == native_height;
	bool viewport = layer->viewport != NULL &&
			(layer->fractional_scale > 0 || !native);
	int32_t scale = 1;
	int32_t width = -1;
	int32_t height = -1;
	if (viewport) {
		// viewport maps any buffer size back to the logical size
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

	send_geometry(layer, buffer);

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
	uint32_t pixel_width;
	uint32_t pixel_height;
	matuwall_layer_buffer_size(layer, &pixel_width, &pixel_height);
	return matuwall_layer_begin_frame_sized(
		layer, shm, pixel_width, pixel_height, out);
}

enum matuwall_buffer_acquire matuwall_layer_begin_frame_sized(
	struct matuwall_layer *layer, struct wl_shm *shm, uint32_t width,
	uint32_t height, struct matuwall_buffer **out) {
	*out = NULL;
	if (!layer->configured || layer->wl_surface == NULL) {
		return MATUWALL_BUFFER_FAILED;
	}
	// requested callback is the compositor's permission for the next
	// frame
	if (layer->frame_callback != NULL) {
		return MATUWALL_BUFFER_BUSY;
	}
	// only a viewport can map an off-size buffer onto the surface
	if (layer->viewport == NULL) {
		uint32_t native_width;
		uint32_t native_height;
		matuwall_layer_buffer_size(
			layer, &native_width, &native_height);
		if (width != native_width || height != native_height) {
			return MATUWALL_BUFFER_FAILED;
		}
	}
	return matuwall_buffer_pool_acquire(
		&layer->buffer_pool, shm, width, height, out);
}

bool matuwall_layer_commit_frame(struct matuwall_layer *layer,
	bool continue_frames, bool opaque,
	const struct matuwall_damage *damage) {
	if (layer->buffer_pool.drawing == NULL) {
		return false;
	}
	return present(layer, continue_frames, opaque, damage);
}
