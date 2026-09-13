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
	double overflow = 0.0;
	for (size_t i = 0; i < frame->focus_count; i++) {
		double extra_x = (frame->focuses[i].scale - 1.0) *
				 frame->layout->tile_width / 2.0;
		double extra_y = (frame->focuses[i].scale - 1.0) *
				 frame->layout->tile_height / 2.0;
		double extra = extra_x > extra_y ? extra_x : extra_y;
		if (extra > overflow) {
			overflow = extra;
		}
	}
	double edge = (frame->shadow >> 24) > 0 ? frame->shadow_width : 0;
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

enum tile_position {
	TILE_ABOVE,
	TILE_VISIBLE,
	TILE_BELOW,
};

struct tile_geometry {
	int32_t left;
	int32_t top;
	int32_t width;
	int32_t height;
	int32_t radius;
	int32_t border;
};

static enum tile_position tile_geometry(const struct matuwall_frame *frame,
	const struct matuwall_clip *clip, size_t index, double focus,
	struct tile_geometry *geometry) {
	struct matuwall_rect item = matuwall_layout_item(frame->layout, index);
	double width = item.width * focus;
	double height = item.height * focus;
	double x = frame->panel.x + item.x - (width - item.width) / 2.0;
	double y = frame->panel.y + item.y - (height - item.height) / 2.0 -
		   frame->scroll;
	int32_t left = scaled(x, frame->scale);
	int32_t top = scaled(y, frame->scale);
	int32_t right = scaled(x + width, frame->scale);
	int32_t bottom = scaled(y + height, frame->scale);
	if (top >= clip->y1) {
		return TILE_BELOW;
	}
	if (bottom <= clip->y0) {
		return TILE_ABOVE;
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
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	const struct tile_geometry *geometry, double focus) {
	int32_t width = scaled(frame->shadow_width * focus, frame->scale);
	if (frame->shadow_width > 0 && width < 1) {
		width = 1;
	}
	matuwall_draw_rounded_shadow(buffer, clip, geometry->left,
		geometry->top, geometry->width, geometry->height,
		geometry->radius, width, frame->shadow);
}

static bool focused(const struct matuwall_frame *frame, size_t index) {
	for (size_t i = 0; i < frame->focus_count; i++) {
		if (frame->focuses[i].index == index) {
			return true;
		}
	}
	return false;
}

static void draw_tile_pass(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	bool shadows) {
	for (size_t i = 0; i < frame->item_count; i++) {
		struct tile_geometry geometry;
		enum tile_position position =
			tile_geometry(frame, clip, i, 1.0, &geometry);
		if (position == TILE_BELOW) {
			break;
		}
		if (position == TILE_ABOVE || focused(frame, i)) {
			continue;
		}
		if (shadows) {
			draw_shadow(buffer, frame, clip, &geometry, 1.0);
		} else {
			draw_tile(buffer, frame, clip, i, &geometry, false);
		}
	}

	for (size_t i = 0; i < frame->focus_count; i++) {
		size_t index = frame->focuses[i].index;
		if (index >= frame->item_count) {
			continue;
		}
		double focus = frame->focuses[i].scale;
		struct tile_geometry geometry;
		if (tile_geometry(frame, clip, index, focus, &geometry) !=
			TILE_VISIBLE) {
			continue;
		}
		if (shadows) {
			draw_shadow(buffer, frame, clip, &geometry, focus);
		} else {
			draw_tile(buffer, frame, clip, index, &geometry, true);
		}
	}
}

static void draw_ring(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	const struct matuwall_frame_ring *ring, int32_t gap, int32_t width) {
	if (ring->alpha == 0) {
		return;
	}
	int32_t left = scaled(frame->panel.x + ring->x, frame->scale);
	int32_t top =
		scaled(frame->panel.y + ring->y - frame->scroll, frame->scale);
	int32_t right =
		scaled(frame->panel.x + ring->x + ring->width, frame->scale);
	int32_t bottom =
		scaled(frame->panel.y + ring->y + ring->height - frame->scroll,
			frame->scale);
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

	struct matuwall_clip clip = content_clip(buffer, frame);

	int32_t ring_gap = to_pixels(RING_GAP, frame->scale);
	int32_t ring_width =
		to_pixels((int32_t)frame->ring_width, frame->scale);
	if (frame->ring_width > 0 && ring_width < 1) {
		ring_width = 1;
	}

	if (frame->shadow_width > 0 && (frame->shadow >> 24) > 0) {
		draw_tile_pass(buffer, frame, &clip, true);
	}
	draw_tile_pass(buffer, frame, &clip, false);

	if (ring_width > 0 && frame->ring.a > 0) {
		for (size_t i = 0; i < frame->ring_count; i++) {
			draw_ring(buffer, frame, &clip, &frame->rings[i],
				ring_gap, ring_width);
		}
	}
}
