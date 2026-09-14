#include "app/animation.h"

#include <math.h>
#include <stdlib.h>

static struct matuwall_animation_rect item_rect(
	const struct matuwall_layout *layout, size_t index) {
	struct matuwall_rect rect = matuwall_layout_item(layout, index);
	return (struct matuwall_animation_rect){
		.x = rect.x,
		.y = rect.y,
		.width = rect.width,
		.height = rect.height,
	};
}

static struct matuwall_animation_rect scale_rect(
	struct matuwall_animation_rect rect, double scale) {
	double width = rect.width * scale;
	double height = rect.height * scale;
	return (struct matuwall_animation_rect){
		.x = rect.x - (width - rect.width) / 2.0,
		.y = rect.y - (height - rect.height) / 2.0,
		.width = width,
		.height = height,
	};
}

static double scroll_for(
	const struct matuwall_layout *layout, uint32_t first_row) {
	return (double)first_row *
	       (double)(layout->tile_height + layout->spacing);
}

static double lerp(double from, double to, double progress) {
	return from + (to - from) * progress;
}

static struct matuwall_animation_rect lerp_rect(
	struct matuwall_animation_rect from, struct matuwall_animation_rect to,
	double progress) {
	return (struct matuwall_animation_rect){
		.x = lerp(from.x, to.x, progress),
		.y = lerp(from.y, to.y, progress),
		.width = lerp(from.width, to.width, progress),
		.height = lerp(from.height, to.height, progress),
	};
}

static double progress_at(
	const struct matuwall_animation *animation, int64_t now_ms) {
	if (animation->kind == MATUWALL_ANIMATION_NONE ||
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

static bool adjacent(const struct matuwall_layout *layout, size_t previous,
	size_t selected) {
	struct matuwall_rect from = matuwall_layout_item(layout, previous);
	struct matuwall_rect to = matuwall_layout_item(layout, selected);
	int32_t dx = abs(to.x - from.x);
	int32_t dy = abs(to.y - from.y);
	uint32_t step_x = layout->tile_width + layout->spacing;
	uint32_t step_y = layout->tile_height + layout->spacing;

	return (dy == 0 && dx == (int32_t)step_x) ||
	       (dx == 0 && dy == (int32_t)step_y);
}

static bool neighboring_row(const struct matuwall_layout *layout,
	size_t previous, size_t selected) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	size_t previous_row = previous / columns;
	size_t selected_row = selected / columns;
	size_t distance = previous_row > selected_row
				  ? previous_row - selected_row
				  : selected_row - previous_row;
	return distance == 1;
}

static void add_focus(
	struct matuwall_animation_sample *sample, size_t index, double scale) {
	if (scale <= 1.0) {
		return;
	}
	for (size_t i = 0; i < sample->focus_count; i++) {
		if (sample->focuses[i].index != index) {
			continue;
		}
		if (scale > sample->focuses[i].scale) {
			sample->focuses[i].scale = scale;
		}
		return;
	}
	if (sample->focus_count < 2) {
		sample->focuses[sample->focus_count++] =
			(struct matuwall_animation_focus){
				.index = index,
				.scale = scale,
			};
	}
}

static double focus_at(
	const struct matuwall_animation_sample *sample, size_t index) {
	for (size_t i = 0; i < sample->focus_count; i++) {
		if (sample->focuses[i].index == index) {
			return sample->focuses[i].scale;
		}
	}
	return 1.0;
}

void matuwall_animation_init(struct matuwall_animation *animation,
	uint32_t duration_ms, uint32_t zoom_percent) {
	*animation = (struct matuwall_animation){
		.duration_ms = duration_ms,
		.focus_scale = 1.0 + (double)zoom_percent / 100.0,
	};
}

void matuwall_animation_snap(struct matuwall_animation *animation,
	const struct matuwall_layout *layout, size_t selected,
	uint32_t first_row) {
	animation->from_ring =
		scale_rect(item_rect(layout, selected), animation->focus_scale);
	animation->to_ring = animation->from_ring;
	animation->from_scroll = scroll_for(layout, first_row);
	animation->to_scroll = animation->from_scroll;
	animation->from_focus = selected;
	animation->to_focus = selected;
	animation->from_focus_scale = animation->focus_scale;
	animation->to_focus_scale = animation->focus_scale;
	animation->kind = MATUWALL_ANIMATION_NONE;
	animation->initialized = true;
}

void matuwall_animation_sample(struct matuwall_animation *animation,
	int64_t now_ms, struct matuwall_animation_sample *sample) {
	*sample = (struct matuwall_animation_sample){0};
	if (!animation->initialized) {
		return;
	}

	double progress = progress_at(animation, now_ms);
	double eased = ease_out_cubic(progress);
	sample->active = progress < 1.0;
	sample->scroll =
		lerp(animation->from_scroll, animation->to_scroll, eased);

	if (animation->kind == MATUWALL_ANIMATION_HANDOFF && progress < 1.0) {
		double fade = smoothstep(progress);
		sample->rings[0] = (struct matuwall_animation_ring){
			.rect = animation->from_ring,
			.alpha = (uint8_t)lround((1.0 - fade) * 255.0),
		};
		sample->rings[1] = (struct matuwall_animation_ring){
			.rect = animation->to_ring,
			.alpha = (uint8_t)lround(fade * 255.0),
		};
		sample->ring_count = 2;
	} else {
		sample->rings[0] = (struct matuwall_animation_ring){
			.rect = animation->kind == MATUWALL_ANIMATION_GLIDE
					? lerp_rect(animation->from_ring,
						  animation->to_ring, eased)
					: animation->to_ring,
			.alpha = 255,
		};
		sample->ring_count = 1;
	}

	if (sample->active) {
		add_focus(sample, animation->from_focus,
			lerp(animation->from_focus_scale, 1.0, eased));
		add_focus(sample, animation->to_focus,
			lerp(animation->to_focus_scale, animation->focus_scale,
				eased));
	} else {
		add_focus(sample, animation->to_focus, animation->focus_scale);
	}
	if (!sample->active) {
		animation->from_ring = animation->to_ring;
		animation->from_scroll = animation->to_scroll;
		animation->from_focus = animation->to_focus;
		animation->from_focus_scale = animation->focus_scale;
		animation->to_focus_scale = animation->focus_scale;
		animation->kind = MATUWALL_ANIMATION_NONE;
	}
}

void matuwall_animation_move(struct matuwall_animation *animation,
	const struct matuwall_layout *layout, size_t previous, size_t selected,
	uint32_t first_row, int64_t now_ms) {
	if (!animation->initialized || animation->duration_ms == 0) {
		matuwall_animation_snap(animation, layout, selected, first_row);
		return;
	}

	struct matuwall_animation_sample current;
	matuwall_animation_sample(animation, now_ms, &current);
	struct matuwall_animation_rect current_ring =
		current.ring_count == 1 ? current.rings[0].rect
					: animation->to_ring;
	struct matuwall_animation_rect target =
		scale_rect(item_rect(layout, selected), animation->focus_scale);
	double target_scroll = scroll_for(layout, first_row);
	double previous_scale = focus_at(&current, previous);
	double selected_scale = focus_at(&current, selected);

	animation->started_ms = now_ms;
	animation->to_ring = target;
	animation->to_scroll = target_scroll;
	animation->from_focus = previous;
	animation->to_focus = selected;
	animation->from_focus_scale = previous_scale;
	animation->to_focus_scale = selected_scale;
	if (adjacent(layout, previous, selected)) {
		animation->from_ring = current_ring;
		animation->from_scroll = current.scroll;
		animation->kind = MATUWALL_ANIMATION_GLIDE;
	} else {
		if (neighboring_row(layout, previous, selected)) {
			animation->from_ring = current_ring;
			animation->from_scroll = current.scroll;
		} else {
			// Preserve the old ring position across distant jumps
			current_ring.y += target_scroll - current.scroll;
			animation->from_ring = current_ring;
			animation->from_scroll = target_scroll;
		}
		animation->kind = MATUWALL_ANIMATION_HANDOFF;
	}
}

double matuwall_animation_scroll(
	struct matuwall_animation *animation, int64_t now_ms) {
	struct matuwall_animation_sample sample;
	matuwall_animation_sample(animation, now_ms, &sample);
	return sample.scroll;
}
