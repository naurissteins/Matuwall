#include "app/animation.h"

#include <math.h>

static struct matuwall_animation_rect item_rect(
	const struct matuwall_layout *layout, int64_t slot) {
	struct matuwall_layout_rect rect = matuwall_layout_slot(layout, slot);
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

static double lerp(double from, double to, double progress) {
	return from + (to - from) * progress;
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

static bool adjacent(const struct matuwall_layout *layout, int64_t previous,
	int64_t selected) {
	struct matuwall_layout_rect from =
		matuwall_layout_slot(layout, previous);
	struct matuwall_layout_rect to = matuwall_layout_slot(layout, selected);
	double dx = fabs(to.x - from.x);
	double dy = fabs(to.y - from.y);
	uint32_t step_x = layout->tile_width + layout->spacing;
	uint32_t step_y = layout->tile_height + layout->spacing;

	return (dy == 0 && dx == step_x) || (dx == 0 && dy == step_y);
}

static int64_t slot_row(int64_t slot, uint32_t columns) {
	int64_t row = slot / columns;
	if (slot < 0 && slot % columns != 0) {
		row--;
	}
	return row;
}

static bool neighboring_row(const struct matuwall_layout *layout,
	int64_t previous, int64_t selected) {
	uint32_t columns = layout->columns == 0 ? 1 : layout->columns;
	int64_t previous_row = slot_row(previous, columns);
	int64_t selected_row = slot_row(selected, columns);
	uint64_t distance = previous_row > selected_row
				    ? (uint64_t)(previous_row - selected_row)
				    : (uint64_t)(selected_row - previous_row);
	return distance == 1;
}

static void add_focus(struct matuwall_animation_sample *sample, size_t index,
	int64_t slot, double scale) {
	if (scale <= 1.0) {
		return;
	}
	for (size_t i = 0; i < sample->focus_count; i++) {
		if (sample->focuses[i].slot != slot) {
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
				.slot = slot,
				.scale = scale,
			};
	}
}

static double focus_at(
	const struct matuwall_animation_sample *sample, int64_t slot) {
	for (size_t i = 0; i < sample->focus_count; i++) {
		if (sample->focuses[i].slot == slot) {
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
	const struct matuwall_layout *layout, const struct matuwall_rect *panel,
	size_t selected, int64_t selected_slot, uint32_t first_row) {
	animation->to_ring = scale_rect(
		item_rect(layout, selected_slot), animation->focus_scale);
	animation->from_scroll =
		matuwall_layout_scroll(layout, panel, selected_slot, first_row);
	animation->to_scroll = animation->from_scroll;
	animation->from_focus = selected;
	animation->to_focus = selected;
	animation->from_slot = selected_slot;
	animation->to_slot = selected_slot;
	animation->from_focus_scale = animation->focus_scale;
	animation->to_focus_scale = animation->focus_scale;
	animation->kind = MATUWALL_ANIMATION_NONE;
	animation->initialized = true;
}

void matuwall_animation_sample(const struct matuwall_animation *animation,
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

	double ring_alpha = animation->kind == MATUWALL_ANIMATION_FADE_IN
				    ? smoothstep(progress)
				    : 1.0;
	sample->rings[0] = (struct matuwall_animation_ring){
		.rect = animation->to_ring,
		.alpha = (uint8_t)lround(ring_alpha * 255.0),
	};
	sample->ring_count = 1;

	if (sample->active) {
		add_focus(sample, animation->from_focus, animation->from_slot,
			lerp(animation->from_focus_scale, 1.0, eased));
		add_focus(sample, animation->to_focus, animation->to_slot,
			lerp(animation->to_focus_scale, animation->focus_scale,
				eased));
	} else {
		add_focus(sample, animation->to_focus, animation->to_slot,
			animation->focus_scale);
	}
}

void matuwall_animation_move(struct matuwall_animation *animation,
	const struct matuwall_layout *layout, const struct matuwall_rect *panel,
	size_t previous, int64_t previous_slot, size_t selected,
	int64_t selected_slot, uint32_t first_row, int64_t now_ms) {
	if (!animation->initialized || animation->duration_ms == 0) {
		matuwall_animation_snap(animation, layout, panel, selected,
			selected_slot, first_row);
		return;
	}

	struct matuwall_animation_sample current;
	matuwall_animation_sample(animation, now_ms, &current);
	struct matuwall_animation_rect target = scale_rect(
		item_rect(layout, selected_slot), animation->focus_scale);
	double target_scroll =
		matuwall_layout_scroll(layout, panel, selected_slot, first_row);
	double previous_scale = focus_at(&current, previous_slot);
	double selected_scale = focus_at(&current, selected_slot);

	animation->started_ms = now_ms;
	animation->to_ring = target;
	animation->to_scroll = target_scroll;
	animation->from_focus = previous;
	animation->to_focus = selected;
	animation->from_slot = previous_slot;
	animation->to_slot = selected_slot;
	animation->from_focus_scale = previous_scale;
	animation->to_focus_scale = selected_scale;
	if (adjacent(layout, previous_slot, selected_slot) ||
		neighboring_row(layout, previous_slot, selected_slot)) {
		animation->from_scroll = current.scroll;
	} else {
		animation->from_scroll = target_scroll;
	}
	animation->kind = MATUWALL_ANIMATION_FADE_IN;
}

double matuwall_animation_scroll(
	const struct matuwall_animation *animation, int64_t now_ms) {
	struct matuwall_animation_sample sample;
	matuwall_animation_sample(animation, now_ms, &sample);
	return sample.scroll;
}
