#include "render/tiles.h"

#include <math.h>
#include <stdbool.h>

#include "render/image.h"
#include "render/spinner.h"

#define SPINNER_DIVISOR 14
// size a fading tile reaches one full step past the margin
#define FADE_MIN_SCALE 0.8

static int32_t scaled(double logical, double scale) {
	return (int32_t)lround(logical * scale);
}

enum tile_position {
	TILE_BEFORE,
	TILE_VISIBLE,
	TILE_AFTER,
};

struct tile_geometry {
	int32_t left;
	int32_t top;
	int32_t width;
	int32_t height;
	int32_t radius;
	int32_t border;
};

enum tile_layer {
	LAYER_SHADOWS,
	LAYER_FADING,
	LAYER_OPAQUE,
};

// The regular grid keeps a hard edge; fading is a carousel treatment
static bool edge_fades(const struct matuwall_frame *frame) {
	return frame->edge == MATUWALL_EDGE_FADE &&
	       frame->layout->flow != MATUWALL_FLOW_GRID;
}

static bool edge_clips(const struct matuwall_frame *frame) {
	return frame->edge == MATUWALL_EDGE_CLIP ||
	       (frame->edge == MATUWALL_EDGE_FADE && !edge_fades(frame));
}

static double smoothstep(double t) {
	return t * t * (3.0 - 2.0 * t);
}

// fade and shrink a tile by how far it reaches past the margin, pinning
// its outer edge there so neighbors slide over it and nothing is clipped
struct matuwall_tile_edge matuwall_tiles_edge(
	const struct matuwall_frame *frame, int64_t slot) {
	struct matuwall_tile_edge edge = {
		.scale = 1.0, .opacity = MATUWALL_OPAQUE};
	if (!edge_fades(frame)) {
		return edge;
	}
	bool horizontal = frame->layout->flow == MATUWALL_FLOW_HORIZONTAL;
	struct matuwall_layout_rect item =
		matuwall_layout_slot(frame->layout, slot);
	double start = (horizontal ? item.x : item.y) - frame->scroll;
	double extent = horizontal ? item.width : item.height;
	double near = frame->layout->margin;
	double far =
		(horizontal ? frame->panel.width : frame->panel.height) - near;
	// viewport narrower than one tile would fade the selection at rest
	if (far - near < extent) {
		return edge;
	}

	double before = near - start;
	double after = start + extent - far;
	double over = before > after ? before : after;
	if (over <= 0.0) {
		return edge;
	}
	double step = extent + frame->layout->spacing;
	double eased = smoothstep(over < step ? over / step : 1.0);
	edge.scale = 1.0 - (1.0 - FADE_MIN_SCALE) * eased;
	double shrink = extent * (1.0 - edge.scale) / 2.0;
	edge.shift = before > after ? before - shrink : shrink - after;
	edge.opacity = (uint8_t)lround(MATUWALL_OPAQUE * (1.0 - eased));
	return edge;
}

static struct matuwall_clip tile_visibility_clip(
	const struct matuwall_frame *frame,
	const struct matuwall_clip *effects) {
	struct matuwall_clip clip = *effects;
	if (!edge_clips(frame)) {
		return clip;
	}

	int32_t left =
		scaled(frame->panel.x + frame->layout->margin, frame->scale);
	int32_t top =
		scaled(frame->panel.y + frame->layout->margin, frame->scale);
	int32_t right = scaled(
		frame->panel.x + frame->panel.width - frame->layout->margin,
		frame->scale);
	int32_t bottom = scaled(
		frame->panel.y + frame->panel.height - frame->layout->margin,
		frame->scale);
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL && left < right) {
		clip.x0 = left > clip.x0 ? left : clip.x0;
		clip.x1 = right < clip.x1 ? right : clip.x1;
	}
	if (frame->layout->flow != MATUWALL_FLOW_HORIZONTAL && top < bottom) {
		clip.y0 = top > clip.y0 ? top : clip.y0;
		clip.y1 = bottom < clip.y1 ? bottom : clip.y1;
	}
	return clip;
}

static enum tile_position tile_geometry(const struct matuwall_frame *frame,
	const struct matuwall_clip *clip, int64_t slot, double focus,
	double shift, struct tile_geometry *geometry) {
	struct matuwall_layout_rect item =
		matuwall_layout_slot(frame->layout, slot);
	double width = item.width * focus;
	double height = item.height * focus;
	double x = frame->panel.x + item.x - (width - item.width) / 2.0;
	double y = frame->panel.y + item.y - (height - item.height) / 2.0;
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		x += shift - frame->scroll;
	} else {
		y += shift - frame->scroll;
	}
	int32_t left = scaled(x, frame->scale);
	int32_t top = scaled(y, frame->scale);
	int32_t right = scaled(x + width, frame->scale);
	int32_t bottom = scaled(y + height, frame->scale);
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		if (left >= clip->x1) {
			return TILE_AFTER;
		}
		if (right <= clip->x0) {
			return TILE_BEFORE;
		}
	} else {
		if (top >= clip->y1) {
			return TILE_AFTER;
		}
		if (bottom <= clip->y0) {
			return TILE_BEFORE;
		}
	}

	int32_t border = scaled(frame->border_width * focus, frame->scale);
	if (frame->border_width > 0 && border < 1) {
		border = 1;
	}
	*geometry = (struct tile_geometry){
		.left = left,
		.top = top,
		.width = right - left,
		.height = bottom - top,
		.radius = scaled(frame->layout->radius * focus, frame->scale),
		.border = border,
	};
	return TILE_VISIBLE;
}

static bool tile_crosses_clip(const struct matuwall_frame *frame,
	const struct matuwall_clip *clip,
	const struct tile_geometry *geometry) {
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		return geometry->left < clip->x0 ||
		       geometry->left + geometry->width > clip->x1;
	}
	return geometry->top < clip->y0 ||
	       geometry->top + geometry->height > clip->y1;
}

static int32_t draw_border(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	const struct tile_geometry *geometry, uint8_t opacity) {
	int32_t border = geometry->border;
	if (border > (geometry->width - 1) / 2 ||
		border > (geometry->height - 1) / 2) {
		return 0;
	}
	if (border <= 0) {
		return border;
	}
	uint32_t color = matuwall_color_fade(frame->border, opacity);
	// translucent tile must not show its border fill through the image
	if (opacity < MATUWALL_OPAQUE) {
		matuwall_draw_rounded_ring(buffer, clip, geometry->left,
			geometry->top, geometry->width, geometry->height,
			geometry->radius, border, color);
	} else {
		matuwall_draw_rounded_rect(buffer, clip, geometry->left,
			geometry->top, geometry->width, geometry->height,
			geometry->radius, color);
	}
	return border;
}

static void draw_tile(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	size_t index, const struct tile_geometry *geometry, bool bilinear,
	uint8_t opacity) {
	const struct matuwall_thumb *thumb =
		frame->thumbs != NULL ? &frame->thumbs[index] : NULL;
	int32_t border = draw_border(buffer, frame, clip, geometry, opacity);

	if (thumb != NULL && thumb->state == MATUWALL_THUMB_READY &&
		thumb->pixels != NULL) {
		void (*draw)(struct matuwall_buffer *,
			const struct matuwall_clip *, int32_t, int32_t, int32_t,
			int32_t, int32_t, int32_t, const uint32_t *, uint32_t,
			uint32_t, uint8_t) =
			bilinear ? matuwall_draw_image_rounded_bilinear
				 : matuwall_draw_image_rounded;
		draw(buffer, clip, geometry->left, geometry->top,
			geometry->width, geometry->height, geometry->radius,
			border, thumb->pixels, thumb->width, thumb->height,
			opacity);
		return;
	}

	int32_t inner_radius =
		geometry->radius > border ? geometry->radius - border : 0;
	matuwall_draw_rounded_rect(buffer, clip, geometry->left + border,
		geometry->top + border, geometry->width - border * 2,
		geometry->height - border * 2, inner_radius,
		matuwall_color_fade(frame->tile, opacity));
	bool pending = thumb == NULL ||
		       thumb->state == MATUWALL_THUMB_UNLOADED ||
		       thumb->state == MATUWALL_THUMB_PENDING;
	if (pending) {
		int32_t shorter = geometry->width < geometry->height
					  ? geometry->width
					  : geometry->height;
		matuwall_spinner_draw(buffer, clip,
			geometry->left + geometry->width / 2,
			geometry->top + geometry->height / 2,
			shorter / SPINNER_DIVISOR, frame->spinner,
			matuwall_alpha_mul(frame->spinner_alpha, opacity));
	}
}

static void draw_shadow(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *effects,
	const struct matuwall_clip *tile_clip,
	const struct tile_geometry *geometry, double focus, uint8_t opacity) {
	int32_t width = scaled(frame->shadow_width * focus, frame->scale);
	if (frame->shadow_width > 0 && width < 1) {
		width = 1;
	}
	struct matuwall_clip shadow_clip = *effects;
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		if (geometry->left < tile_clip->x0) {
			shadow_clip.x0 = tile_clip->x0;
		}
		if (geometry->left + geometry->width > tile_clip->x1) {
			shadow_clip.x1 = tile_clip->x1;
		}
	} else {
		if (geometry->top < tile_clip->y0) {
			shadow_clip.y0 = tile_clip->y0;
		}
		if (geometry->top + geometry->height > tile_clip->y1) {
			shadow_clip.y1 = tile_clip->y1;
		}
	}
	matuwall_draw_rounded_shadow(buffer, &shadow_clip, geometry->left,
		geometry->top, geometry->width, geometry->height,
		geometry->radius, width,
		matuwall_color_fade(frame->shadow, opacity));
}

static bool focused(const struct matuwall_frame *frame, int64_t slot) {
	for (size_t i = 0; i < frame->focus_count; i++) {
		if (frame->focuses[i].slot == slot) {
			return true;
		}
	}
	return false;
}

static int64_t grid_first_slot(
	const struct matuwall_frame *frame, const struct matuwall_clip *clip) {
	if (frame->scale <= 0.0) {
		return 0;
	}
	uint32_t columns =
		frame->layout->columns == 0 ? 1 : frame->layout->columns;
	double step =
		(double)frame->layout->tile_height + frame->layout->spacing;
	if (step <= 0.0) {
		return 0;
	}
	double clip_top = (double)clip->y0 / frame->scale;
	double boundary = (clip_top - frame->panel.y - frame->layout->margin +
				  frame->scroll - frame->layout->tile_height) /
			  step;
	if (!(boundary > 0.0)) {
		return 0;
	}

	// Keep one row before the exact visibility boundary for rounding
	double row = floor(boundary);
	size_t last_row = (frame->item_count - 1) / columns;
	if (row > (double)last_row) {
		return (int64_t)frame->item_count;
	}
	return (int64_t)((size_t)row * columns);
}

static void slot_bounds(const struct matuwall_frame *frame,
	const struct matuwall_clip *clip, int64_t *first, int64_t *last) {
	if (frame->item_count == 0) {
		*first = 1;
		*last = 0;
		return;
	}
	if (frame->layout->flow == MATUWALL_FLOW_GRID) {
		*first = grid_first_slot(frame, clip);
		*last = (int64_t)(frame->item_count - 1);
		return;
	}

	uint32_t extent = frame->layout->flow == MATUWALL_FLOW_HORIZONTAL
				  ? (uint32_t)frame->panel.width
				  : (uint32_t)frame->panel.height;
	uint32_t step =
		frame->layout->flow == MATUWALL_FLOW_HORIZONTAL
			? frame->layout->tile_width + frame->layout->spacing
			: frame->layout->tile_height + frame->layout->spacing;
	int64_t radius = step > 0 ? (int64_t)(extent / step) + 2 : 2;
	*first = frame->carousel_slot >= INT64_MIN + radius
			 ? frame->carousel_slot - radius
			 : INT64_MIN;
	*last = frame->carousel_slot <= INT64_MAX - radius
			? frame->carousel_slot + radius
			: INT64_MAX;
}

static size_t slot_index(const struct matuwall_frame *frame, int64_t slot) {
	if (frame->layout->flow == MATUWALL_FLOW_GRID) {
		return (size_t)slot;
	}
	return matuwall_layout_carousel_index(slot, frame->item_count);
}

static void draw_unfocused_pass(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	enum tile_layer layer) {
	struct matuwall_clip tile_clip = tile_visibility_clip(frame, clip);
	int64_t first;
	int64_t last;
	slot_bounds(frame, &tile_clip, &first, &last);
	for (int64_t slot = first; slot <= last; slot++) {
		size_t index = slot_index(frame, slot);
		struct matuwall_tile_edge edge =
			matuwall_tiles_edge(frame, slot);
		struct tile_geometry geometry;
		enum tile_position position = tile_geometry(frame, &tile_clip,
			slot, edge.scale, edge.shift, &geometry);
		if (position == TILE_AFTER) {
			break;
		}
		bool fading = edge.opacity < MATUWALL_OPAQUE;
		bool skip = position == TILE_BEFORE || edge.opacity == 0 ||
			    focused(frame, slot) ||
			    (layer != LAYER_SHADOWS &&
				    fading != (layer == LAYER_FADING));
		if (!skip && layer == LAYER_SHADOWS) {
			draw_shadow(buffer, frame, clip, &tile_clip, &geometry,
				edge.scale,
				matuwall_alpha_mul(edge.opacity, edge.opacity));
		} else if (!skip) {
			draw_tile(buffer, frame, &tile_clip, index, &geometry,
				fading, edge.opacity);
		}
		if (slot == INT64_MAX) {
			break;
		}
	}
}

static void draw_focused_tiles(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	bool shadows) {
	for (size_t i = 0; i < frame->focus_count; i++) {
		size_t index = frame->focuses[i].index;
		int64_t slot = frame->focuses[i].slot;
		if (index >= frame->item_count) {
			continue;
		}
		struct matuwall_tile_edge edge =
			matuwall_tiles_edge(frame, slot);
		double focus = frame->focuses[i].scale * edge.scale;
		struct tile_geometry geometry;
		if (edge.opacity == 0 ||
			tile_geometry(frame, clip, slot, focus, edge.shift,
				&geometry) != TILE_VISIBLE) {
			continue;
		}
		struct matuwall_clip tile_clip = *clip;
		if (slot != frame->carousel_slot && edge_clips(frame)) {
			struct matuwall_clip viewport =
				tile_visibility_clip(frame, clip);
			struct tile_geometry base;
			if (tile_geometry(frame, clip, slot, 1.0, 0.0, &base) ==
					TILE_VISIBLE &&
				tile_crosses_clip(frame, &viewport, &base)) {
				tile_clip = viewport;
			}
		}
		if (shadows) {
			draw_shadow(buffer, frame, clip, &tile_clip, &geometry,
				focus,
				matuwall_alpha_mul(edge.opacity, edge.opacity));
		}
		draw_tile(buffer, frame, &tile_clip, index, &geometry, true,
			edge.opacity);
	}
}

void matuwall_tiles_draw(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip) {
	bool shadows = frame->shadow_width > 0 && (frame->shadow >> 24) > 0;
	if (shadows) {
		draw_unfocused_pass(buffer, frame, clip, LAYER_SHADOWS);
	}
	// fading tiles go first so the sliding strip passes over them
	if (edge_fades(frame)) {
		draw_unfocused_pass(buffer, frame, clip, LAYER_FADING);
	}
	draw_unfocused_pass(buffer, frame, clip, LAYER_OPAQUE);
	draw_focused_tiles(buffer, frame, clip, shadows);
}
