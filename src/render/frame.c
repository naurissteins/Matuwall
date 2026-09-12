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

static struct sweetwall_clip content_clip(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame) {
	struct sweetwall_clip clip = sweetwall_clip_buffer(buffer);
	int32_t inset = (int32_t)frame->layout->margin - RING_GAP -
			(int32_t)frame->ring_width;
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

static void draw_panel(struct sweetwall_buffer *buffer,
	const struct sweetwall_frame *frame, int32_t radius) {
	struct sweetwall_clip full = sweetwall_clip_buffer(buffer);
	int32_t left = to_pixels(frame->panel.x, frame->scale);
	int32_t top = to_pixels(frame->panel.y, frame->scale);
	int32_t right =
		to_pixels(frame->panel.x + frame->panel.width, frame->scale);
	int32_t bottom =
		to_pixels(frame->panel.y + frame->panel.height, frame->scale);

	sweetwall_draw_rounded_rect(buffer, &full, left, top, right - left,
		bottom - top, radius, frame->background);
}

void sweetwall_frame_draw(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame) {
	int32_t radius =
		to_pixels((int32_t)frame->layout->radius, frame->scale);
	int32_t panel_radius =
		to_pixels((int32_t)frame->panel_radius, frame->scale);

	if (frame->backdrop && frame->preview != NULL) {
		sweetwall_draw_image_cover(buffer, frame->preview,
			frame->preview_width, frame->preview_height);
	} else {
		// Let the real desktop show outside the rounded panel
		sweetwall_draw_clear(buffer, 0);
	}
	draw_panel(buffer, frame, panel_radius);

	struct sweetwall_clip clip = content_clip(buffer, frame);

	int32_t ring_gap = to_pixels(RING_GAP, frame->scale);
	int32_t ring_width =
		to_pixels((int32_t)frame->ring_width, frame->scale);
	if (ring_width < 1) {
		ring_width = 1;
	}
	int32_t border_width =
		to_pixels((int32_t)frame->border_width, frame->scale);
	if (frame->border_width > 0 && border_width < 1) {
		border_width = 1;
	}

	for (size_t i = 0; i < frame->item_count; i++) {
		struct sweetwall_rect rect =
			sweetwall_layout_item(frame->layout, i);
		rect.x += frame->panel.x;
		rect.y += frame->panel.y - frame->scroll;

		int32_t top = to_pixels(rect.y, frame->scale);
		if (top >= clip.y1) {
			break;
		}

		int32_t bottom = to_pixels(rect.y + rect.height, frame->scale);
		if (bottom <= clip.y0) {
			continue;
		}

		int32_t left = to_pixels(rect.x, frame->scale);
		int32_t right = to_pixels(rect.x + rect.width, frame->scale);
		int32_t tw = right - left;
		int32_t th = bottom - top;

		const struct sweetwall_thumb *thumb =
			frame->thumbs != NULL ? &frame->thumbs[i] : NULL;
		int32_t border_inset = 0;
		if (border_width > 0 && border_width <= (tw - 1) / 2 &&
			border_width <= (th - 1) / 2) {
			border_inset = border_width;
			sweetwall_draw_rounded_rect(buffer, &clip, left, top,
				tw, th, radius, frame->border);
		}

		if (thumb != NULL && thumb->state == SWEETWALL_THUMB_READY &&
			thumb->pixels != NULL) {
			sweetwall_draw_image_rounded(buffer, &clip, left, top,
				tw, th, radius, border_inset, thumb->pixels,
				thumb->width, thumb->height);
		} else {
			int32_t inner_radius = radius > border_inset
						       ? radius - border_inset
						       : 0;
			sweetwall_draw_rounded_rect(buffer, &clip,
				left + border_inset, top + border_inset,
				tw - border_inset * 2, th - border_inset * 2,
				inner_radius, frame->tile);

			// A dot only while the decode is still in flight
			bool pending = thumb == NULL ||
				       thumb->state == SWEETWALL_THUMB_PENDING;
			if (pending) {
				int32_t dot =
					(tw < th ? tw : th) / SPINNER_DIVISOR;
				sweetwall_spinner_draw(buffer, &clip,
					left + tw / 2, top + th / 2, dot,
					frame->spinner, frame->spinner_alpha);
			}
		}

		if (i == frame->selected) {
			int32_t inset = ring_gap + ring_width;
			int32_t ring_radius = radius == 0 ? 0 : radius + inset;
			sweetwall_draw_rounded_ring(buffer, &clip, left - inset,
				top - inset, (right - left) + inset * 2,
				(bottom - top) + inset * 2, ring_radius,
				ring_width, frame->ring);
		}
	}
}
