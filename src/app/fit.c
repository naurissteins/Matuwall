#include "app/fit.h"

#include "app/app.h"
#include "app/thumbs.h"
#include "util/log.h"

// --- panel geometry ---

// layer margin that places panel against its anchored edge
static uint32_t edge_offset(const struct matuwall_rect *panel,
	enum matuwall_position position, uint32_t width, uint32_t height) {
	switch (position) {
	case MATUWALL_POSITION_LEFT:
		return (uint32_t)panel->x;
	case MATUWALL_POSITION_RIGHT:
		return width - (uint32_t)(panel->x + panel->width);
	case MATUWALL_POSITION_TOP:
		return (uint32_t)panel->y;
	case MATUWALL_POSITION_BOTTOM:
		return height - (uint32_t)(panel->y + panel->height);
	case MATUWALL_POSITION_CENTER:
		break;
	}
	return 0;
}

struct matuwall_layer_panel matuwall_app_fit_panel(struct matuwall_app *app) {
	matuwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, app->config.carousel,
		app->config.position, app->config.edge_margin, &app->layout,
		&app->visible_rows);
	// off-screen rows cost buffer, so the panel surface fits the output
	struct matuwall_rect rect = matuwall_layout_panel(&app->layout,
		app->scan.count, app->visible_rows, app->config.position,
		app->config.edge_margin, app->output_width, app->output_height);
	return (struct matuwall_layer_panel){
		.width = (uint32_t)rect.width,
		.height = (uint32_t)rect.height,
		.position = app->config.position,
		.margin = edge_offset(&rect, app->config.position,
			app->output_width, app->output_height),
	};
}

static bool same_layout(
	const struct matuwall_layout *a, const struct matuwall_layout *b) {
	return a->columns == b->columns && a->spacing == b->spacing &&
	       a->margin == b->margin && a->tile_width == b->tile_width &&
	       a->tile_height == b->tile_height && a->radius == b->radius &&
	       a->flow == b->flow;
}

static bool same_rect(
	const struct matuwall_rect *a, const struct matuwall_rect *b) {
	return a->x == b->x && a->y == b->y && a->width == b->width &&
	       a->height == b->height;
}

void matuwall_app_fit_refresh(struct matuwall_app *app) {
	struct matuwall_layout old_layout = app->layout;
	struct matuwall_rect old_panel = app->panel;
	uint32_t old_first_row = app->grid.first_row;
	matuwall_layout_adapt(&app->config.layout, app->config.visible_rows,
		app->output_width, app->output_height, app->config.carousel,
		app->config.position, app->config.edge_margin, &app->layout,
		&app->visible_rows);
	app->panel = matuwall_layout_panel(&app->layout, app->scan.count,
		app->visible_rows, app->config.position,
		app->config.edge_margin, app->layer.width, app->layer.height);
	matuwall_grid_reveal(
		&app->grid, &app->layout, (uint32_t)app->panel.height);
	if (!same_layout(&old_layout, &app->layout) ||
		!same_rect(&old_panel, &app->panel) ||
		old_first_row != app->grid.first_row) {
		matuwall_animation_snap(&app->animation, &app->layout,
			&app->panel, app->grid.selected, app->grid.cursor,
			app->grid.first_row);
	}
	matuwall_app_thumbs_prioritize_visible(app);
	app->layer.layout_dirty = false;
}

// --- output changes ---

static void take_output_size(
	struct matuwall_app *app, uint32_t width, uint32_t height) {
	if (width == app->output_width && height == app->output_height) {
		return;
	}
	matuwall_log_info("output", "changed to %ux%u", width, height);
	app->output_width = width;
	app->output_height = height;
	struct matuwall_layer_panel panel = matuwall_app_fit_panel(app);
	matuwall_layer_resize_panel(&app->layer, &panel);
	// refit now, the panel configure refits again at its new size
	app->layer.layout_dirty = true;
	app->layer.needs_repaint = true;
}

// a probe is a bufferless full-output surface, the same one startup uses
static void start_probe(struct matuwall_app *app) {
	struct matuwall_outputs *outputs = &app->registry.outputs;
	if (!outputs->changed) {
		return;
	}
	outputs->changed = false;
	// the surfaces' own output, a NULL fallback lets the compositor choose
	struct wl_output *output = matuwall_outputs_entered(outputs);
	if (output == NULL) {
		output = app->registry.selected_output;
	}
	if (!matuwall_layer_create(&app->probe, &app->registry, output,
		    MATUWALL_LAYER_BACKDROP, NULL)) {
		matuwall_log_warn(
			"output", "cannot measure the changed output");
		matuwall_layer_destroy(&app->probe);
	}
}

void matuwall_app_fit_follow(struct matuwall_app *app) {
	struct matuwall_layer *backdrop = &app->backdrop;
	// the backdrop spans the output, so its configures already follow it
	if (backdrop->wl_surface != NULL) {
		if (backdrop->layout_dirty && backdrop->configured) {
			backdrop->layout_dirty = false;
			take_output_size(
				app, backdrop->width, backdrop->height);
		}
		return;
	}

	struct matuwall_layer *probe = &app->probe;
	if (probe->wl_surface == NULL) {
		start_probe(app);
		return;
	}
	if (probe->configured) {
		take_output_size(app, probe->width, probe->height);
	}
	if (probe->configured || probe->closed) {
		matuwall_layer_destroy(probe);
	}
}
