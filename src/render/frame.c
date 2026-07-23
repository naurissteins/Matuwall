#include "render/frame.h"

#include <math.h>
#include <stdbool.h>

#include "render/draw.h"
#include "render/spinner.h"

#define RING_GAP 3
#define RING_WIDTH 2
#define RING_INSET (RING_GAP + RING_WIDTH)
#define SPINNER_DIVISOR 14

static int32_t to_pixels(int32_t logical, double scale) {
	return (int32_t)lround((double)logical * scale);
}

static struct sweetwall_clip content_clip(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame) {
	struct sweetwall_clip clip = sweetwall_clip_buffer(buffer);
	int32_t inset = (int32_t)frame->layout->margin - RING_INSET;
	if (inset < 0) {
		inset = 0;
	}
	int32_t margin = to_pixels(inset, frame->scale);

	int32_t right =
		to_pixels((int32_t)frame->surface_width, frame->scale) - margin;
	int32_t bottom =
		to_pixels((int32_t)frame->surface_height, frame->scale) -
		margin;

	if (margin > clip.x0) {
		clip.x0 = margin;
	}
	if (margin > clip.y0) {
		clip.y0 = margin;
	}
	if (right < clip.x1) {
		clip.x1 = right;
	}
	if (bottom < clip.y1) {
		clip.y1 = bottom;
	}
	return clip;
}

void sweetwall_frame_draw(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame) {
	sweetwall_draw_clear(buffer, frame->background);

	struct sweetwall_clip clip = content_clip(buffer, frame);
	int32_t radius =
		to_pixels((int32_t)frame->layout->radius, frame->scale);

	int32_t ring_gap = to_pixels(RING_GAP, frame->scale);
	int32_t ring_width = to_pixels(RING_WIDTH, frame->scale);
	if (ring_width < 1) {
		ring_width = 1;
	}

	for (size_t i = 0; i < frame->item_count; i++) {
		struct sweetwall_rect rect =
			sweetwall_layout_item(frame->layout, i);
		rect.y -= frame->scroll;

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

		if (thumb != NULL && thumb->state == SWEETWALL_THUMB_READY &&
			thumb->pixels != NULL) {
			sweetwall_draw_image_rounded(buffer, &clip, left, top,
				tw, th, radius, thumb->pixels, thumb->width,
				thumb->height);
		} else {
			sweetwall_draw_rounded_rect(buffer, &clip, left, top,
				tw, th, radius, frame->tile);

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
			sweetwall_draw_rounded_ring(buffer, &clip, left - inset,
				top - inset, (right - left) + inset * 2,
				(bottom - top) + inset * 2, radius + inset,
				ring_width, frame->ring);
		}
	}
}
