#ifndef MATUWALL_APP_ANIMATION_H
#define MATUWALL_APP_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"

struct matuwall_animation_rect {
	double x;
	double y;
	double width;
	double height;
};

struct matuwall_animation_ring {
	struct matuwall_animation_rect rect;
	uint8_t alpha;
};

struct matuwall_animation_focus {
	size_t index;
	int64_t slot;
	double scale;
};

struct matuwall_animation_sample {
	double scroll;
	struct matuwall_animation_ring rings[2];
	size_t ring_count;
	struct matuwall_animation_focus focuses[2];
	size_t focus_count;
	bool active;
};

enum matuwall_animation_kind {
	MATUWALL_ANIMATION_NONE,
	MATUWALL_ANIMATION_GLIDE,
	MATUWALL_ANIMATION_HANDOFF,
};

// Visual state only; grid selection remains authoritative
struct matuwall_animation {
	uint32_t duration_ms;
	int64_t started_ms;
	double from_scroll;
	double to_scroll;
	struct matuwall_animation_rect from_ring;
	struct matuwall_animation_rect to_ring;
	double focus_scale;
	size_t from_focus;
	size_t to_focus;
	int64_t from_slot;
	int64_t to_slot;
	double from_focus_scale;
	double to_focus_scale;
	// Transition type, not liveness; progress determines completion
	enum matuwall_animation_kind kind;
	bool initialized;
};

void matuwall_animation_init(struct matuwall_animation *animation,
	uint32_t duration_ms, uint32_t zoom_percent);

void matuwall_animation_snap(struct matuwall_animation *animation,
	const struct matuwall_layout *layout, const struct matuwall_rect *panel,
	size_t selected, int64_t selected_slot, uint32_t first_row);

void matuwall_animation_move(struct matuwall_animation *animation,
	const struct matuwall_layout *layout, const struct matuwall_rect *panel,
	size_t previous, int64_t previous_slot, size_t selected,
	int64_t selected_slot, uint32_t first_row, int64_t now_ms);

void matuwall_animation_sample(const struct matuwall_animation *animation,
	int64_t now_ms, struct matuwall_animation_sample *sample);

double matuwall_animation_scroll(
	const struct matuwall_animation *animation, int64_t now_ms);

#endif
