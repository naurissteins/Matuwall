#include "app/animation.h"

#include <math.h>
#include <stdlib.h>

static struct sweetwall_animation_rect item_rect(
	const struct sweetwall_layout *layout, size_t index) {
	struct sweetwall_rect rect = sweetwall_layout_item(layout, index);
	return (struct sweetwall_animation_rect){
		.x = rect.x,
		.y = rect.y,
		.width = rect.width,
		.height = rect.height,
	};
}

static double scroll_for(
	const struct sweetwall_layout *layout, uint32_t first_row) {
	return (double)first_row *
	       (double)(layout->tile_height + layout->spacing);
}

static double lerp(double from, double to, double progress) {
	return from + (to - from) * progress;
}

static struct sweetwall_animation_rect lerp_rect(
	struct sweetwall_animation_rect from,
	struct sweetwall_animation_rect to, double progress) {
	return (struct sweetwall_animation_rect){
		.x = lerp(from.x, to.x, progress),
		.y = lerp(from.y, to.y, progress),
		.width = lerp(from.width, to.width, progress),
		.height = lerp(from.height, to.height, progress),
	};
}

static double progress_at(
	const struct sweetwall_animation *animation, int64_t now_ms) {
	if (animation->kind == SWEETWALL_ANIMATION_NONE ||
		animation->duration_ms == 0) {
		return 1.0;
	}
	int64_t elapsed = now_ms - animation->started_ms;
	if (elapsed <= 0) {
		return 0.0;
	}
	double progress = (double)elapsed / animation->duration_ms;
	return progress < 1.0 ? progress : 1.0;
}

static double ease_out_cubic(double progress) {
	double left = 1.0 - progress;
	return 1.0 - left * left * left;
}

static double smoothstep(double progress) {
	return progress * progress * (3.0 - 2.0 * progress);
}

static bool adjacent(const struct sweetwall_layout *layout, size_t previous,
	size_t selected) {
	struct sweetwall_rect from = sweetwall_layout_item(layout, previous);
	struct sweetwall_rect to = sweetwall_layout_item(layout, selected);
	int32_t dx = abs(to.x - from.x);
	int32_t dy = abs(to.y - from.y);
	uint32_t step_x = layout->tile_width + layout->spacing;
	uint32_t step_y = layout->tile_height + layout->spacing;

	return (dy == 0 && dx == (int32_t)step_x) ||
	       (dx == 0 && dy == (int32_t)step_y);
}

void sweetwall_animation_init(
	struct sweetwall_animation *animation, uint32_t duration_ms) {
	*animation = (struct sweetwall_animation){
		.duration_ms = duration_ms,
	};
}

void sweetwall_animation_snap(struct sweetwall_animation *animation,
	const struct sweetwall_layout *layout, size_t selected,
	uint32_t first_row) {
	animation->from_ring = item_rect(layout, selected);
	animation->to_ring = animation->from_ring;
	animation->from_scroll = scroll_for(layout, first_row);
	animation->to_scroll = animation->from_scroll;
	animation->kind = SWEETWALL_ANIMATION_NONE;
	animation->initialized = true;
}

void sweetwall_animation_sample(struct sweetwall_animation *animation,
	int64_t now_ms, struct sweetwall_animation_sample *sample) {
	*sample = (struct sweetwall_animation_sample){0};
	if (!animation->initialized) {
		return;
	}

	double progress = progress_at(animation, now_ms);
	double eased = ease_out_cubic(progress);
	sample->active = progress < 1.0;
	sample->scroll =
		lerp(animation->from_scroll, animation->to_scroll, eased);

	if (animation->kind == SWEETWALL_ANIMATION_HANDOFF && progress < 1.0) {
		double fade = smoothstep(progress);
		sample->rings[0] = (struct sweetwall_animation_ring){
			.rect = animation->from_ring,
			.alpha = (uint8_t)lround((1.0 - fade) * 255.0),
		};
		sample->rings[1] = (struct sweetwall_animation_ring){
			.rect = animation->to_ring,
			.alpha = (uint8_t)lround(fade * 255.0),
		};
		sample->ring_count = 2;
		return;
	}

	sample->rings[0] = (struct sweetwall_animation_ring){
		.rect = animation->kind == SWEETWALL_ANIMATION_GLIDE
				? lerp_rect(animation->from_ring,
					  animation->to_ring, eased)
				: animation->to_ring,
		.alpha = 255,
	};
	sample->ring_count = 1;
	if (!sample->active) {
		animation->from_ring = animation->to_ring;
		animation->from_scroll = animation->to_scroll;
		animation->kind = SWEETWALL_ANIMATION_NONE;
	}
}

void sweetwall_animation_move(struct sweetwall_animation *animation,
	const struct sweetwall_layout *layout, size_t previous, size_t selected,
	uint32_t first_row, int64_t now_ms) {
	if (!animation->initialized || animation->duration_ms == 0) {
		sweetwall_animation_snap(
			animation, layout, selected, first_row);
		return;
	}

	struct sweetwall_animation_sample current;
	sweetwall_animation_sample(animation, now_ms, &current);
	struct sweetwall_animation_rect current_ring =
		current.ring_count == 1 ? current.rings[0].rect
					: animation->to_ring;
	struct sweetwall_animation_rect target = item_rect(layout, selected);
	double target_scroll = scroll_for(layout, first_row);

	animation->started_ms = now_ms;
	animation->to_ring = target;
	animation->to_scroll = target_scroll;
	if (adjacent(layout, previous, selected)) {
		animation->from_ring = current_ring;
		animation->from_scroll = current.scroll;
		animation->kind = SWEETWALL_ANIMATION_GLIDE;
	} else {
		// Preserve the old on-screen ring while content snaps to its
		// target
		current_ring.y += target_scroll - current.scroll;
		animation->from_ring = current_ring;
		animation->from_scroll = target_scroll;
		animation->kind = SWEETWALL_ANIMATION_HANDOFF;
	}
}

double sweetwall_animation_scroll(
	struct sweetwall_animation *animation, int64_t now_ms) {
	struct sweetwall_animation_sample sample;
	sweetwall_animation_sample(animation, now_ms, &sample);
	return sample.scroll;
}
