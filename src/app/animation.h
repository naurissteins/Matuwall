#ifndef SWEETWALL_APP_ANIMATION_H
#define SWEETWALL_APP_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"

struct sweetwall_animation_rect {
	double x;
	double y;
	double width;
	double height;
};

struct sweetwall_animation_ring {
	struct sweetwall_animation_rect rect;
	uint8_t alpha;
};

struct sweetwall_animation_focus {
	size_t index;
	double scale;
};

struct sweetwall_animation_sample {
	double scroll;
	struct sweetwall_animation_ring rings[2];
	size_t ring_count;
	struct sweetwall_animation_focus focuses[2];
	size_t focus_count;
	bool active;
};

enum sweetwall_animation_kind {
	SWEETWALL_ANIMATION_NONE,
	SWEETWALL_ANIMATION_GLIDE,
	SWEETWALL_ANIMATION_HANDOFF,
};

// Visual state only; grid selection remains authoritative
struct sweetwall_animation {
	uint32_t duration_ms;
	int64_t started_ms;
	double from_scroll;
	double to_scroll;
	struct sweetwall_animation_rect from_ring;
	struct sweetwall_animation_rect to_ring;
	double focus_scale;
	size_t from_focus;
	size_t to_focus;
	double from_focus_scale;
	double to_focus_scale;
	enum sweetwall_animation_kind kind;
	bool initialized;
};

void sweetwall_animation_init(struct sweetwall_animation *animation,
	uint32_t duration_ms, uint32_t zoom_percent);

void sweetwall_animation_snap(struct sweetwall_animation *animation,
	const struct sweetwall_layout *layout, size_t selected,
	uint32_t first_row);

void sweetwall_animation_move(struct sweetwall_animation *animation,
	const struct sweetwall_layout *layout, size_t previous, size_t selected,
	uint32_t first_row, int64_t now_ms);

void sweetwall_animation_sample(struct sweetwall_animation *animation,
	int64_t now_ms, struct sweetwall_animation_sample *sample);

double sweetwall_animation_scroll(
	struct sweetwall_animation *animation, int64_t now_ms);

#endif
