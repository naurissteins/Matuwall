#include "render/frame.h"

#include <math.h>
#include <stdbool.h>

#include "render/draw.h"
#include "render/spinner.h"

#define RING_GAP 3
#define SPINNER_DIVISOR 14

static int32_t to_pixels(int32_t logical, double scale) {
	return (int32_t)lround((double)logical * scale);
}

static int32_t scaled(double logical, double scale) {
	return (int32_t)lround(logical * scale);
}

static uint32_t ring_color(struct matuwall_color color, uint8_t alpha) {
	color.a = (uint8_t)(((uint32_t)color.a * alpha + 127) / 255);
	return matuwall_color_argb(color);
}

static int32_t content_overflow(const struct matuwall_frame *frame) {
	double focus = frame->focus_scale > 1.0 ? frame->focus_scale : 1.0;
	double extra_x = (focus - 1.0) * frame->layout->tile_width / 2.0;
	double extra_y = (focus - 1.0) * frame->layout->tile_height / 2.0;
	double overflow = extra_x > extra_y ? extra_x : extra_y;
	double edge =
		(frame->shadow >> 24) > 0 ? frame->shadow_width * focus : 0;
	if (frame->ring_width > 0 && frame->ring.a > 0) {
		double ring = RING_GAP + frame->ring_width;
		if (ring > edge) {
			edge = ring;
		}
	}
	return (int32_t)ceil(overflow + edge);
}

static struct matuwall_clip content_clip(
	struct matuwall_buffer *buffer, const struct matuwall_frame *frame) {
	struct matuwall_clip clip = matuwall_clip_buffer(buffer);
	int32_t inset =
		(int32_t)frame->layout->margin - content_overflow(frame);
	if (inset < 0) {
		inset = 0;
	}

	int32_t left = to_pixels(frame->panel.x + inset, frame->scale);
	int32_t top = to_pixels(frame->panel.y + inset, frame->scale);
	int32_t right = to_pixels(
		frame->panel.x + frame->panel.width - inset, frame->scale);
	int32_t bottom = to_pixels(
		frame->panel.y + frame->panel.height - inset, frame->scale);

	if (left > clip.x0) {
		clip.x0 = left;
	}
	if (top > clip.y0) {
		clip.y0 = top;
	}
	if (right < clip.x1) {
		clip.x1 = right;
	}
	if (bottom < clip.y1) {
		clip.y1 = bottom;
	}
	return clip;
}

static void draw_panel(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, int32_t radius) {
	struct matuwall_clip full = matuwall_clip_buffer(buffer);
	int32_t left = to_pixels(frame->panel.x, frame->scale);
	int32_t top = to_pixels(frame->panel.y, frame->scale);
	int32_t right =
		to_pixels(frame->panel.x + frame->panel.width, frame->scale);
	int32_t bottom =
		to_pixels(frame->panel.y + frame->panel.height, frame->scale);

	matuwall_draw_rounded_rect(buffer, &full, left, top, right - left,
		bottom - top, radius, frame->background);
}

static void draw_directory_unavailable(
	struct matuwall_buffer *buffer, const struct matuwall_frame *frame) {
	struct matuwall_clip clip = matuwall_clip_buffer(buffer);
	int32_t panel_width = to_pixels(frame->panel.width, frame->scale);
	int32_t panel_height = to_pixels(frame->panel.height, frame->scale);
	int32_t size =
		(panel_width < panel_height ? panel_width : panel_height) / 3;
	int32_t maximum = to_pixels(112, frame->scale);
	if (size > maximum) {
		size = maximum;
	}
	if (size < 12) {
		return;
	}

	int32_t center_x = to_pixels(
		frame->panel.x + frame->panel.width / 2, frame->scale);
	int32_t center_y = to_pixels(
		frame->panel.y + frame->panel.height / 2, frame->scale);
	int32_t body_width = size;
	int32_t body_height = size * 5 / 8;
	int32_t body_x = center_x - body_width / 2;
	int32_t body_y = center_y - body_height / 2 + size / 12;
	int32_t stroke = to_pixels(3, frame->scale);
	if (stroke < 1) {
		stroke = 1;
	}
	struct matuwall_color icon = frame->spinner;
	if (icon.a == 0) {
		icon = frame->ring;
	}
	if (icon.a == 0) {
		icon = (struct matuwall_color){0xf2, 0xf2, 0xf2, 0xff};
	}
	uint32_t color = matuwall_color_argb(icon);
	bool light = (uint32_t)icon.r * 299 + (uint32_t)icon.g * 587 +
			     (uint32_t)icon.b * 114 >
		     128000;
	uint32_t mark = light ? 0xff181825 : 0xfff2f2f2;

	matuwall_draw_rounded_rect(buffer, &clip, body_x, body_y, body_width,
		body_height, size / 14, color);
	matuwall_draw_rounded_rect(buffer, &clip, body_x + size / 10,
		body_y - size / 7, size * 2 / 5, size / 5, size / 20, color);

	int32_t mark_width = stroke * 2;
	int32_t mark_x = center_x - mark_width / 2;
	matuwall_draw_rounded_rect(buffer, &clip, mark_x,
		body_y + body_height / 4, mark_width, body_height / 3,
		mark_width / 2, mark);
	matuwall_draw_rounded_rect(buffer, &clip, mark_x,
		body_y + body_height * 3 / 4, mark_width, mark_width,
		mark_width / 2, mark);
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

static void draw_tile(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	size_t index, const struct tile_geometry *geometry, bool bilinear) {
	const struct matuwall_thumb *thumb =
		frame->thumbs != NULL ? &frame->thumbs[index] : NULL;
	int32_t border = geometry->border;
	if (border > (geometry->width - 1) / 2 ||
		border > (geometry->height - 1) / 2) {
		border = 0;
	}
	if (border > 0) {
		matuwall_draw_rounded_rect(buffer, clip, geometry->left,
			geometry->top, geometry->width, geometry->height,
			geometry->radius, frame->border);
	}

	if (thumb != NULL && thumb->state == MATUWALL_THUMB_READY &&
		thumb->pixels != NULL) {
		void (*draw)(struct matuwall_buffer *,
			const struct matuwall_clip *, int32_t, int32_t, int32_t,
			int32_t, int32_t, int32_t, const uint32_t *, uint32_t,
			uint32_t) =
			bilinear ? matuwall_draw_image_rounded_bilinear
				 : matuwall_draw_image_rounded;
		draw(buffer, clip, geometry->left, geometry->top,
			geometry->width, geometry->height, geometry->radius,
			border, thumb->pixels, thumb->width, thumb->height);
		return;
	}

	int32_t inner_radius =
		geometry->radius > border ? geometry->radius - border : 0;
	matuwall_draw_rounded_rect(buffer, clip, geometry->left + border,
		geometry->top + border, geometry->width - border * 2,
		geometry->height - border * 2, inner_radius, frame->tile);
	bool pending = thumb == NULL || thumb->state == MATUWALL_THUMB_PENDING;
	if (pending) {
		int32_t shorter = geometry->width < geometry->height
					  ? geometry->width
					  : geometry->height;
		matuwall_spinner_draw(buffer, clip,
			geometry->left + geometry->width / 2,
			geometry->top + geometry->height / 2,
			shorter / SPINNER_DIVISOR, frame->spinner,
			frame->spinner_alpha);
	}
}

static void draw_shadow(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *effects,
	const struct matuwall_clip *tile_clip,
	const struct tile_geometry *geometry, double focus) {
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
		geometry->radius, width, frame->shadow);
}

static bool focused(const struct matuwall_frame *frame, int64_t slot) {
	for (size_t i = 0; i < frame->focus_count; i++) {
		if (frame->focuses[i].slot == slot) {
			return true;
		}
	}
	return false;
}

static void slot_bounds(
	const struct matuwall_frame *frame, int64_t *first, int64_t *last) {
	if (frame->item_count == 0) {
		*first = 1;
		*last = 0;
		return;
	}
	if (frame->layout->flow == MATUWALL_FLOW_GRID) {
		*first = 0;
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
	slot_bounds(frame, &first, &last);
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
				1.0);
		} else {
			draw_tile(buffer, frame, &tile_clip, index, &geometry,
				false);
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
				focus);
		}
		draw_tile(buffer, frame, &tile_clip, index, &geometry, true);
	}
}

static void draw_ring(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	const struct matuwall_frame_ring *ring, int32_t gap, int32_t width) {
	if (ring->alpha == 0) {
		return;
	}
	int32_t left = scaled(frame->panel.x + ring->x, frame->scale);
	int32_t top = scaled(frame->panel.y + ring->y, frame->scale);
	int32_t right =
		scaled(frame->panel.x + ring->x + ring->width, frame->scale);
	int32_t bottom =
		scaled(frame->panel.y + ring->y + ring->height, frame->scale);
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		int32_t offset = scaled(frame->scroll, frame->scale);
		left -= offset;
		right -= offset;
	} else {
		int32_t offset = scaled(frame->scroll, frame->scale);
		top -= offset;
		bottom -= offset;
	}
	int32_t inset = gap + width;
	double focus = frame->layout->tile_width > 0
			       ? ring->width / frame->layout->tile_width
			       : 1.0;
	int32_t radius = scaled(frame->layout->radius * focus, frame->scale);
	int32_t ring_radius = radius == 0 ? 0 : radius + inset;
	matuwall_draw_rounded_ring(buffer, clip, left - inset, top - inset,
		(right - left) + inset * 2, (bottom - top) + inset * 2,
		ring_radius, width, ring_color(frame->ring, ring->alpha));
}

void matuwall_frame_draw(
	struct matuwall_buffer *buffer, const struct matuwall_frame *frame) {
	int32_t panel_radius =
		to_pixels((int32_t)frame->panel_radius, frame->scale);

	if (frame->backdrop && frame->preview != NULL) {
		matuwall_draw_image_cover(buffer, frame->preview,
			frame->preview_width, frame->preview_height);
	} else {
		// Let the real desktop show outside the rounded panel
		matuwall_draw_clear(buffer, 0);
	}
	draw_panel(buffer, frame, panel_radius);
	if (frame->directory_unavailable) {
		draw_directory_unavailable(buffer, frame);
		return;
	}

	struct matuwall_clip clip = content_clip(buffer, frame);

	int32_t ring_gap = to_pixels(RING_GAP, frame->scale);
	int32_t ring_width =
		to_pixels((int32_t)frame->ring_width, frame->scale);
	if (frame->ring_width > 0 && ring_width < 1) {
		ring_width = 1;
	}

	bool shadows = frame->shadow_width > 0 && (frame->shadow >> 24) > 0;
	if (shadows) {
		draw_unfocused_pass(buffer, frame, &clip, true);
	}
	draw_unfocused_pass(buffer, frame, &clip, false);
	draw_focused_tiles(buffer, frame, &clip, shadows);

	if (ring_width > 0 && frame->ring.a > 0) {
		for (size_t i = 0; i < frame->ring_count; i++) {
			draw_ring(buffer, frame, &clip, &frame->rings[i],
				ring_gap, ring_width);
		}
	}
}
