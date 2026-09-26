#ifndef MATUWALL_WAYLAND_LAYER_H
#define MATUWALL_WAYLAND_LAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "grid/layout.h"
#include "wayland/registry.h"
#include "wayland/shm.h"

struct zwlr_layer_surface_v1;
struct wl_callback;
struct wp_viewport;
struct wp_fractional_scale_v1;

// which of the picker's surfaces this is, each has its own namespace
enum matuwall_layer_role {
	// keyboard-exclusive grid panel, namespace "matuwall"
	MATUWALL_LAYER_PANEL,
	// full-output preview behind the panel, namespace "matuwall-preview"
	MATUWALL_LAYER_BACKDROP,
};

// compact placement against one output edge, or centered
struct matuwall_layer_panel {
	uint32_t width;
	uint32_t height;
	enum matuwall_position position;
	uint32_t margin;
};

struct matuwall_layer {
	// Non-owning, the registry outlives the layer
	struct wl_compositor *compositor;
	struct wl_surface *wl_surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;

	uint32_t width;
	uint32_t height;
	// Client choice when a configure leaves a dimension unspecified
	uint32_t requested_width;
	uint32_t requested_height;
	uint32_t fractional_scale;
	int32_t buffer_scale;
	uint32_t opaque_width;
	uint32_t opaque_height;

	// last buffer scale and viewport destination sent; -1 means unset
	int32_t sent_scale;
	int32_t sent_width;
	int32_t sent_height;
	bool geometry_sent;

	bool configured;
	bool needs_repaint;
	bool layout_dirty;
	bool opaque;
	bool closed;
	// frequent repaints make one released buffer worth keeping at idle
	bool keep_spare;

	struct wl_callback *frame_callback;
	struct matuwall_buffer_pool buffer_pool;
	// stretched transparent pixel that maps the surface before any content
	struct wl_buffer *clear_buffer;
	// surplus buffers are freed once this passes quietly, 0 when collected
	int64_t collect_due_ms;
};

// starts bufferless; a NULL panel spans the output so its bounds are known
bool matuwall_layer_create(struct matuwall_layer *layer,
	const struct matuwall_registry *reg, struct wl_output *output,
	enum matuwall_layer_role role,
	const struct matuwall_layer_panel *panel);

// Replace the output probe with the final compact panel geometry
void matuwall_layer_set_panel(
	struct matuwall_layer *layer, const struct matuwall_layer_panel *panel);

// map a configured surface fully transparent, ahead of its first frame
bool matuwall_layer_map_clear(struct matuwall_layer *layer, struct wl_shm *shm);

// an unmapped surface may not know its scale yet, borrow a sibling's
void matuwall_layer_inherit_scale(
	struct matuwall_layer *layer, const struct matuwall_layer *from);

void matuwall_layer_buffer_size(const struct matuwall_layer *layer,
	uint32_t *pixel_width, uint32_t *pixel_height);

enum matuwall_buffer_acquire matuwall_layer_begin_frame(
	struct matuwall_layer *layer, struct wl_shm *shm,
	struct matuwall_buffer **buffer);

// a buffer at any size, scaled to the surface, needs a viewport unless native
enum matuwall_buffer_acquire matuwall_layer_begin_frame_sized(
	struct matuwall_layer *layer, struct wl_shm *shm, uint32_t width,
	uint32_t height, struct matuwall_buffer **buffer);

bool matuwall_layer_commit_frame(struct matuwall_layer *layer,
	bool continue_frames, bool opaque,
	const struct matuwall_damage *damage);

// poll deadline for idle collection, -1 when nothing is due
int matuwall_layer_idle_timeout(
	const struct matuwall_layer *layer, int64_t now_ms);

// release surplus buffers after a quiet period with no repaint waiting
void matuwall_layer_collect_idle(struct matuwall_layer *layer, int64_t now_ms);

void matuwall_layer_destroy(struct matuwall_layer *layer);

#endif
