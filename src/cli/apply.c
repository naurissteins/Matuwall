#include "cli/options.h"

#include <string.h>

void matuwall_cli_apply(const struct matuwall_cli_options *options,
	struct matuwall_config *config) {
	if (options->background_set) {
		config->background = options->background;
	}
	if (options->tile_set) {
		config->tile = options->tile;
	}
	if (options->border_set) {
		config->border = options->border;
	}
	if (options->border_width_set) {
		config->border_width = options->border_width;
	}
	if (options->ring_set) {
		config->ring = options->ring;
	}
	if (options->ring_width_set) {
		config->ring_width = options->ring_width;
	}
	if (options->spinner_set) {
		config->spinner = options->spinner;
	}
	if (options->backend_set) {
		memcpy(config->backend, options->backend,
			strlen(options->backend) + 1);
	}
	if (options->columns_set) {
		config->layout.columns = options->columns;
	}
	if (options->height_set) {
		config->layout.tile_height = options->height;
	}
	if (options->margin_set) {
		config->layout.margin = options->margin;
	}
	if (options->hooks_set) {
		config->on_apply_count = options->hook_count;
		for (size_t i = 0; i < options->hook_count; i++) {
			memcpy(config->on_apply[i], options->hooks[i],
				strlen(options->hooks[i]) + 1);
		}
	}
	if (options->panel_radius_set) {
		config->panel_radius = options->panel_radius;
	}
	if (options->radius_set) {
		config->layout.radius = options->radius;
	}
	if (options->rows_set) {
		config->visible_rows = options->rows;
	}
	if (options->spacing_set) {
		config->layout.spacing = options->spacing;
	}
	if (options->width_set) {
		config->layout.tile_width = options->width;
	}
	if (options->directory_set) {
		memcpy(config->directory, options->directory,
			strlen(options->directory) + 1);
	}
	if (options->position_set) {
		config->position = options->position;
	}
	if (options->preview_set) {
		config->preview = options->preview;
	}
	if (options->close_on_focus_loss_set) {
		config->close_on_focus_loss = options->close_on_focus_loss;
	}
}
