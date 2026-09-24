#include "render/tiles.h"

#include <math.h>
#include <stdbool.h>

#include "render/image.h"
#include "render/spinner.h"

#define SPINNER_DIVISOR 14

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

static struct matuwall_clip tile_visibility_clip(
	const struct matuwall_frame *frame,
	const struct matuwall_clip *effects) {
	struct matuwall_clip clip = *effects;
	if (frame->edge_peek) {
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
	struct tile_geometry *geometry) {
	struct matuwall_layout_rect item =
		matuwall_layout_slot(frame->layout, slot);
	double width = item.width * focus;
	double height = item.height * focus;
	double x = frame->panel.x + item.x - (width - item.width) / 2.0;
	double y = frame->panel.y + item.y - (height - item.height) / 2.0;
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		x -= frame->scroll;
	} else {
		y -= frame->scroll;
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
	bool shadows) {
	struct matuwall_clip tile_clip = tile_visibility_clip(frame, clip);
	int64_t first;
	int64_t last;
	slot_bounds(frame, &tile_clip, &first, &last);
	for (int64_t slot = first; slot <= last; slot++) {
		size_t index = slot_index(frame, slot);
		struct tile_geometry geometry;
		enum tile_position position =
			tile_geometry(frame, &tile_clip, slot, 1.0, &geometry);
		if (position == TILE_AFTER) {
			break;
		}
		if (position == TILE_BEFORE || focused(frame, slot)) {
			continue;
		}
		if (shadows) {
			draw_shadow(buffer, frame, clip, &tile_clip, &geometry,
				1.0, MATUWALL_OPAQUE);
		} else {
			draw_tile(buffer, frame, &tile_clip, index, &geometry,
				false, MATUWALL_OPAQUE);
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
		double focus = frame->focuses[i].scale;
		struct tile_geometry geometry;
		if (tile_geometry(frame, clip, slot, focus, &geometry) !=
			TILE_VISIBLE) {
			continue;
		}
		struct matuwall_clip tile_clip = *clip;
		if (slot != frame->carousel_slot && !frame->edge_peek) {
			struct matuwall_clip viewport =
				tile_visibility_clip(frame, clip);
			struct tile_geometry base;
			if (tile_geometry(frame, clip, slot, 1.0, &base) ==
					TILE_VISIBLE &&
				tile_crosses_clip(frame, &viewport, &base)) {
				tile_clip = viewport;
			}
		}
		if (shadows) {
			draw_shadow(buffer, frame, clip, &tile_clip, &geometry,
				focus, MATUWALL_OPAQUE);
		}
		draw_tile(buffer, frame, &tile_clip, index, &geometry, true,
			MATUWALL_OPAQUE);
	}
}

void matuwall_tiles_draw(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip) {
	bool shadows = frame->shadow_width > 0 && (frame->shadow >> 24) > 0;
	if (shadows) {
		draw_unfocused_pass(buffer, frame, clip, true);
	}
	draw_unfocused_pass(buffer, frame, clip, false);
	draw_focused_tiles(buffer, frame, clip, shadows);
}
