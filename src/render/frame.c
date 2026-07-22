#include "render/frame.h"

#include <math.h>

#include "render/draw.h"

static int32_t to_pixels(int32_t logical, double scale) {
	return (int32_t)lround((double)logical * scale);
}

static struct sweetwall_clip content_clip(
	struct sweetwall_buffer *buffer, const struct sweetwall_frame *frame) {
	struct sweetwall_clip clip = sweetwall_clip_buffer(buffer);
	int32_t margin =
		to_pixels((int32_t)frame->layout->margin, frame->scale);

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

	for (size_t i = 0; i < frame->item_count; i++) {
		struct sweetwall_rect rect =
			sweetwall_layout_item(frame->layout, i);

		int32_t top = to_pixels(rect.y, frame->scale);
		if (top >= clip.y1) {
			break;
		}

		int32_t left = to_pixels(rect.x, frame->scale);
		int32_t right = to_pixels(rect.x + rect.width, frame->scale);
		int32_t bottom = to_pixels(rect.y + rect.height, frame->scale);

		sweetwall_draw_rounded_rect(buffer, &clip, left, top,
			right - left, bottom - top, radius, frame->tile);
	}
}
