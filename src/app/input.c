#include "app/input.h"

#include <stddef.h>

#include "app/app.h"
#include "app/preview.h"
#include "app/thumbs.h"
#include "util/clock.h"
#include "util/log.h"

// Selection changed: animate presentation and let the backdrop follow later
static void selection_changed(struct matuwall_app *app, size_t previous,
	int64_t previous_cursor, int64_t now_ms) {
	matuwall_animation_move(&app->animation, &app->layout, &app->panel,
		previous, previous_cursor, app->grid.selected, app->grid.cursor,
		app->grid.first_row, now_ms);
	app->layer.needs_repaint = true;
	matuwall_app_thumbs_prioritize_visible(app);
	matuwall_app_preview_select(app, app->grid.selected, now_ms);
}

static void apply_and_exit(struct matuwall_app *app) {
	if (app->scan.count > 0) {
		app->apply_requested = true;
	}
	app->running = false;
}

// --- keyboard ---

static void handle_key(void *user_data, xkb_keysym_t sym) {
	struct matuwall_app *app = user_data;
	enum matuwall_move move;

	switch (sym) {
	case XKB_KEY_Escape:
		matuwall_log_info("exit", "cancelled by Escape");
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
		move = MATUWALL_MOVE_LEFT;
		break;
	case XKB_KEY_Right:
	case XKB_KEY_l:
		move = MATUWALL_MOVE_RIGHT;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_k:
		move = MATUWALL_MOVE_UP;
		break;
	case XKB_KEY_Down:
	case XKB_KEY_j:
		move = MATUWALL_MOVE_DOWN;
		break;
	case XKB_KEY_Prior:
		move = MATUWALL_MOVE_PAGE_UP;
		break;
	case XKB_KEY_Next:
		move = MATUWALL_MOVE_PAGE_DOWN;
		break;
	case XKB_KEY_Home:
	case XKB_KEY_g:
		move = MATUWALL_MOVE_FIRST;
		break;
	case XKB_KEY_End:
	case XKB_KEY_G:
		move = MATUWALL_MOVE_LAST;
		break;
	default:
		return;
	}

	size_t previous = app->grid.selected;
	int64_t previous_cursor = app->grid.cursor;
	if (matuwall_grid_move(&app->grid, &app->layout,
		    (uint32_t)app->panel.height, move)) {
		selection_changed(
			app, previous, previous_cursor, matuwall_now_ms());
	}
}

static void handle_focus_lost(void *user_data) {
	struct matuwall_app *app = user_data;
	if (!app->running || !app->config.close_on_focus_loss) {
		return;
	}
	matuwall_log_info("exit", "cancelled after keyboard focus was lost");
	app->running = false;
}

// --- pointer ---

// Hover selects the tile under the cursor
static void handle_pointer_motion(
	void *user_data, struct wl_surface *surface, int32_t x, int32_t y) {
	struct matuwall_app *app = user_data;
	// the backdrop has no tiles, and its coordinates are not the panel's
	if (surface != app->layer.wl_surface) {
		return;
	}
	int64_t now_ms = matuwall_now_ms();
	double scroll = matuwall_animation_scroll(&app->animation, now_ms);
	int64_t slot;
	size_t hit = matuwall_layout_hit(&app->layout, &app->panel, scroll,
		app->scan.count, &slot, x, y);
	size_t previous = app->grid.selected;
	int64_t previous_cursor = app->grid.cursor;
	if (hit != SIZE_MAX &&
		matuwall_grid_select(&app->grid, &app->layout,
			(uint32_t)app->panel.height, hit, slot)) {
		selection_changed(app, previous, previous_cursor, now_ms);
	}
}

// A left click on a tile picks it, like Enter; off the panel it cancels
static void handle_pointer_button(void *user_data, struct wl_surface *surface,
	int32_t x, int32_t y, bool pressed) {
	struct matuwall_app *app = user_data;
	if (!pressed) {
		return;
	}
	size_t hit = SIZE_MAX;
	int64_t slot = 0;
	if (surface == app->layer.wl_surface) {
		double scroll = matuwall_animation_scroll(
			&app->animation, matuwall_now_ms());
		hit = matuwall_layout_hit(&app->layout, &app->panel, scroll,
			app->scan.count, &slot, x, y);
	}
	if (hit == SIZE_MAX) {
		// only a backdrop surface has anywhere to click past the panel
		if (app->config.preview) {
			matuwall_log_info(
				"exit", "cancelled by click outside the panel");
			app->running = false;
		}
		return;
	}
	matuwall_grid_select(&app->grid, &app->layout,
		(uint32_t)app->panel.height, hit, slot);
	apply_and_exit(app);
}

static void handle_pointer_scroll(void *user_data, int32_t steps) {
	struct matuwall_app *app = user_data;
	bool horizontal = app->layout.flow == MATUWALL_FLOW_HORIZONTAL;
	enum matuwall_move move = steps > 0 ? (horizontal ? MATUWALL_MOVE_RIGHT
							  : MATUWALL_MOVE_DOWN)
					    : (horizontal ? MATUWALL_MOVE_LEFT
							  : MATUWALL_MOVE_UP);
	int32_t count = steps > 0 ? steps : -steps;
	bool changed = false;
	size_t previous = app->grid.selected;
	int64_t previous_cursor = app->grid.cursor;
	for (int32_t i = 0; i < count; i++) {
		changed |= matuwall_grid_move(&app->grid, &app->layout,
			(uint32_t)app->panel.height, move);
	}
	if (changed) {
		selection_changed(
			app, previous, previous_cursor, matuwall_now_ms());
	}
}

static const struct matuwall_seat_handler keyboard_handler = {
	.key = handle_key,
	.focus_lost = handle_focus_lost,
};

static const struct matuwall_seat_handler mouse_handler = {
	.key = handle_key,
	.focus_lost = handle_focus_lost,
	.pointer_motion = handle_pointer_motion,
	.pointer_button = handle_pointer_button,
	.pointer_scroll = handle_pointer_scroll,
};

const struct matuwall_seat_handler *matuwall_app_input_handler(
	bool mouse_enabled) {
	return mouse_enabled ? &mouse_handler : &keyboard_handler;
}
