#include "render/frame.h"

#include <math.h>
#include <stdbool.h>

#include "render/backdrop.h"
#include "render/draw.h"
#include "render/tiles.h"

#define RING_GAP 3

static int32_t to_pixels(int32_t logical, double scale) {
	return (int32_t)lround((double)logical * scale);
}

static int32_t scaled(double logical, double scale) {
	return (int32_t)lround(logical * scale);
}

static struct matuwall_damage full_damage(
	const struct matuwall_buffer *buffer) {
	return (struct matuwall_damage){
		.x1 = (int32_t)buffer->width,
		.y1 = (int32_t)buffer->height,
	};
}

static struct matuwall_damage clip_damage(
	struct matuwall_damage damage, const struct matuwall_buffer *buffer) {
	if (damage.x0 < 0) {
		damage.x0 = 0;
	}
	if (damage.y0 < 0) {
		damage.y0 = 0;
	}
	if (damage.x1 > (int32_t)buffer->width) {
		damage.x1 = (int32_t)buffer->width;
	}
	if (damage.y1 > (int32_t)buffer->height) {
		damage.y1 = (int32_t)buffer->height;
	}
	return damage;
}

static struct matuwall_damage union_damage(
	struct matuwall_damage a, struct matuwall_damage b) {
	return (struct matuwall_damage){
		.x0 = a.x0 < b.x0 ? a.x0 : b.x0,
		.y0 = a.y0 < b.y0 ? a.y0 : b.y0,
		.x1 = a.x1 > b.x1 ? a.x1 : b.x1,
		.y1 = a.y1 > b.y1 ? a.y1 : b.y1,
	};
}

static struct matuwall_damage panel_damage(const struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame) {
	return clip_damage(
		(struct matuwall_damage){
			.x0 = to_pixels(frame->panel.x, frame->scale),
			.y0 = to_pixels(frame->panel.y, frame->scale),
			.x1 = to_pixels(frame->panel.x + frame->panel.width,
				frame->scale),
			.y1 = to_pixels(frame->panel.y + frame->panel.height,
				frame->scale),
		},
		buffer);
}

static struct matuwall_clip damage_clip(struct matuwall_damage damage) {
	return (struct matuwall_clip){
		.x0 = damage.x0,
		.y0 = damage.y0,
		.x1 = damage.x1,
		.y1 = damage.y1,
	};
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

static struct matuwall_clip content_clip(const struct matuwall_frame *frame,
	const struct matuwall_clip *damage) {
	struct matuwall_clip clip = *damage;
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
	const struct matuwall_frame *frame, const struct matuwall_clip *clip,
	int32_t radius) {
	int32_t left = to_pixels(frame->panel.x, frame->scale);
	int32_t top = to_pixels(frame->panel.y, frame->scale);
	int32_t right =
		to_pixels(frame->panel.x + frame->panel.width, frame->scale);
	int32_t bottom =
		to_pixels(frame->panel.y + frame->panel.height, frame->scale);

	matuwall_draw_rounded_rect(buffer, clip, left, top, right - left,
		bottom - top, radius, frame->background);
}

static void draw_directory_unavailable(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, const struct matuwall_clip *clip) {
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

	matuwall_draw_rounded_rect(buffer, clip, body_x, body_y, body_width,
		body_height, size / 14, color);
	matuwall_draw_rounded_rect(buffer, clip, body_x + size / 10,
		body_y - size / 7, size * 2 / 5, size / 5, size / 20, color);

	int32_t mark_width = stroke * 2;
	int32_t mark_x = center_x - mark_width / 2;
	matuwall_draw_rounded_rect(buffer, clip, mark_x,
		body_y + body_height / 4, mark_width, body_height / 3,
		mark_width / 2, mark);
	matuwall_draw_rounded_rect(buffer, clip, mark_x,
		body_y + body_height * 3 / 4, mark_width, mark_width,
		mark_width / 2, mark);
}

// ring follows its tile through the carousel edge fade
static struct matuwall_frame_ring edge_ring(const struct matuwall_frame *frame,
	const struct matuwall_frame_ring *ring) {
	struct matuwall_tile_edge edge =
		matuwall_tiles_edge(frame, frame->carousel_slot);
	struct matuwall_frame_ring out = *ring;
	out.width = ring->width * edge.scale;
	out.height = ring->height * edge.scale;
	out.x += (ring->width - out.width) / 2.0;
	out.y += (ring->height - out.height) / 2.0;
	if (frame->layout->flow == MATUWALL_FLOW_HORIZONTAL) {
		out.x += edge.shift;
	} else {
		out.y += edge.shift;
	}
	out.alpha = matuwall_alpha_mul(ring->alpha, edge.opacity);
	return out;
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

struct matuwall_damage matuwall_frame_draw(struct matuwall_buffer *buffer,
	const struct matuwall_frame *frame, uint64_t backdrop_generation) {
	int32_t panel_radius =
		to_pixels((int32_t)frame->panel_radius, frame->scale);
	struct matuwall_damage overlay = panel_damage(buffer, frame);
	bool full = !buffer->frame_valid ||
		    buffer->backdrop_generation != backdrop_generation;
	struct matuwall_damage damage =
		full ? full_damage(buffer)
		     : clip_damage(
			       union_damage(buffer->overlay_damage, overlay),
			       buffer);
	if (damage.x0 >= damage.x1 || damage.y0 >= damage.y1) {
		full = true;
		damage = full_damage(buffer);
	}
	struct matuwall_clip damaged = damage_clip(damage);

	if (frame->backdrop) {
		bool opaque = matuwall_backdrop_draw(buffer, &frame->preview,
			damage, overlay, backdrop_generation, full);
		buffer->backdrop_opaque =
			opaque && (full || buffer->backdrop_opaque);
	} else {
		// Let the real desktop show outside the rounded panel
		matuwall_draw_clear_clipped(buffer, &damaged, 0);
		buffer->backdrop_opaque = false;
	}
	draw_panel(buffer, frame, &damaged, panel_radius);
	buffer->frame_valid = true;
	buffer->backdrop_generation = backdrop_generation;
	buffer->overlay_damage = overlay;
	if (frame->directory_unavailable) {
		draw_directory_unavailable(buffer, frame, &damaged);
		return damage;
	}

	struct matuwall_clip clip = content_clip(frame, &damaged);

	int32_t ring_gap = to_pixels(RING_GAP, frame->scale);
	int32_t ring_width =
		to_pixels((int32_t)frame->ring_width, frame->scale);
	if (frame->ring_width > 0 && ring_width < 1) {
		ring_width = 1;
	}

	matuwall_tiles_draw(buffer, frame, &clip);

	if (ring_width > 0 && frame->ring.a > 0) {
		for (size_t i = 0; i < frame->ring_count; i++) {
			struct matuwall_frame_ring ring =
				edge_ring(frame, &frame->rings[i]);
			draw_ring(buffer, frame, &clip, &ring, ring_gap,
				ring_width);
		}
	}
	return damage;
}
