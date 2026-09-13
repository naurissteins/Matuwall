#include "app/input.h"

#include <math.h>
#include <stddef.h>

#include "app/app.h"
#include "app/preview.h"
#include "app/thumbs.h"
#include "util/clock.h"
#include "util/log.h"

// Selection changed: animate presentation and let the backdrop follow later
static void selection_changed(
	struct sweetwall_app *app, size_t previous, int64_t now_ms) {
	sweetwall_animation_move(&app->animation, &app->layout, previous,
		app->grid.selected, app->grid.first_row, now_ms);
	app->layer.needs_repaint = true;
	sweetwall_app_thumbs_prioritize_visible(app);
	sweetwall_app_preview_select(app, app->grid.selected, now_ms);
}

static void apply_and_exit(struct sweetwall_app *app) {
	if (app->scan.count > 0) {
		app->apply_requested = true;
	}
	app->running = false;
}

// --- keyboard ---

static void handle_key(void *user_data, xkb_keysym_t sym) {
	struct sweetwall_app *app = user_data;
	enum sweetwall_move move;

	switch (sym) {
	case XKB_KEY_Escape:
		sweetwall_log_info("exit", "cancelled by Escape");
		app->running = false;
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		// Defer the apply off the input path; run() does it on the way
		// out
		apply_and_exit(app);
		return;
	case XKB_KEY_Left:
	case XKB_KEY_h:
		move = SWEETWALL_MOVE_LEFT;
		break;
	case XKB_KEY_Right:
	case XKB_KEY_l:
		move = SWEETWALL_MOVE_RIGHT;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_k:
		move = SWEETWALL_MOVE_UP;
		break;
	case XKB_KEY_Down:
	case XKB_KEY_j:
		move = SWEETWALL_MOVE_DOWN;
		break;
	case XKB_KEY_Prior:
		move = SWEETWALL_MOVE_PAGE_UP;
		break;
	case XKB_KEY_Next:
		move = SWEETWALL_MOVE_PAGE_DOWN;
		break;
	case XKB_KEY_Home:
	case XKB_KEY_g:
		move = SWEETWALL_MOVE_FIRST;
		break;
	case XKB_KEY_End:
	case XKB_KEY_G:
		move = SWEETWALL_MOVE_LAST;
		break;
	default:
		return;
	}

	size_t previous = app->grid.selected;
	if (sweetwall_grid_move(&app->grid, &app->layout,
		    (uint32_t)app->panel.height, move)) {
		selection_changed(app, previous, sweetwall_now_ms());
	}
}

static void handle_focus_lost(void *user_data) {
	struct sweetwall_app *app = user_data;
	if (!app->running || !app->config.close_on_focus_loss) {
		return;
	}
	sweetwall_log_info("exit", "cancelled after keyboard focus was lost");
	app->running = false;
}

// --- pointer ---

// Hover selects the tile under the cursor
static void handle_pointer_motion(void *user_data, int32_t x, int32_t y) {
	struct sweetwall_app *app = user_data;
	int64_t now_ms = sweetwall_now_ms();
	int32_t scroll = (int32_t)lround(
		sweetwall_animation_scroll(&app->animation, now_ms));
	size_t hit = sweetwall_layout_hit(
		&app->layout, &app->panel, scroll, app->scan.count, x, y);
	size_t previous = app->grid.selected;
	if (hit != SIZE_MAX && sweetwall_grid_select(&app->grid, &app->layout,
				       (uint32_t)app->panel.height, hit)) {
		selection_changed(app, previous, now_ms);
	}
}

// A left click on a tile picks it, like Enter; off the panel it cancels
static void handle_pointer_button(
	void *user_data, int32_t x, int32_t y, bool pressed) {
	struct sweetwall_app *app = user_data;
	if (!pressed) {
		return;
	}
	int32_t scroll = (int32_t)lround(sweetwall_animation_scroll(
		&app->animation, sweetwall_now_ms()));
	size_t hit = sweetwall_layout_hit(
		&app->layout, &app->panel, scroll, app->scan.count, x, y);
	if (hit == SIZE_MAX) {
		// Only a backdrop surface has anywhere to click past the panel
		if (app->config.preview) {
			sweetwall_log_info(
				"exit", "cancelled by click outside the panel");
			app->running = false;
		}
		return;
	}
	sweetwall_grid_select(
		&app->grid, &app->layout, (uint32_t)app->panel.height, hit);
	apply_and_exit(app);
}

static void handle_pointer_scroll(void *user_data, int32_t steps) {
	struct sweetwall_app *app = user_data;
	enum sweetwall_move move =
		steps > 0 ? SWEETWALL_MOVE_DOWN : SWEETWALL_MOVE_UP;
	int32_t count = steps > 0 ? steps : -steps;
	bool changed = false;
	size_t previous = app->grid.selected;
	for (int32_t i = 0; i < count; i++) {
		changed |= sweetwall_grid_move(&app->grid, &app->layout,
			(uint32_t)app->panel.height, move);
	}
	if (changed) {
		selection_changed(app, previous, sweetwall_now_ms());
	}
}

static const struct sweetwall_seat_handler keyboard_handler = {
	.key = handle_key,
	.focus_lost = handle_focus_lost,
};

static const struct sweetwall_seat_handler mouse_handler = {
	.key = handle_key,
	.focus_lost = handle_focus_lost,
	.pointer_motion = handle_pointer_motion,
	.pointer_button = handle_pointer_button,
	.pointer_scroll = handle_pointer_scroll,
};

const struct sweetwall_seat_handler *sweetwall_app_input_handler(
	bool mouse_enabled) {
	return mouse_enabled ? &mouse_handler : &keyboard_handler;
}
