#include "cli/options.h"

#include <string.h>

#include "util/log.h"

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
	if (options->shadow_set) {
		config->shadow = options->shadow;
	}
	if (options->shadow_width_set) {
		config->shadow_width = options->shadow_width;
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
	if (options->carousel_set) {
		config->carousel = options->carousel;
	}
	if (options->height_set) {
		config->layout.tile_height = options->height;
	}
	if (options->margin_set) {
		config->layout.margin = options->margin;
	}
	if (options->edge_margin_set) {
		config->edge_margin = options->edge_margin;
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

void matuwall_cli_log_overrides(const struct matuwall_cli_options *options,
	const struct matuwall_config *config) {
	if (options->background_set) {
		matuwall_log_info("config",
			"background overridden to #%02x%02x%02x%02x",
			config->background.r, config->background.g,
			config->background.b, config->background.a);
	}
	if (options->tile_set) {
		matuwall_log_info("config",
			"tile overridden to #%02x%02x%02x%02x", config->tile.r,
			config->tile.g, config->tile.b, config->tile.a);
	}
	if (options->border_set) {
		matuwall_log_info("config",
			"border overridden to #%02x%02x%02x%02x",
			config->border.r, config->border.g, config->border.b,
			config->border.a);
	}
	if (options->border_width_set) {
		matuwall_log_info("config", "border width overridden to %u",
			config->border_width);
	}
	if (options->shadow_set) {
		matuwall_log_info("config",
			"shadow overridden to #%02x%02x%02x%02x",
			config->shadow.r, config->shadow.g, config->shadow.b,
			config->shadow.a);
	}
	if (options->shadow_width_set) {
		matuwall_log_info("config", "shadow width overridden to %u",
			config->shadow_width);
	}
	if (options->ring_set) {
		matuwall_log_info("config",
			"ring overridden to #%02x%02x%02x%02x", config->ring.r,
			config->ring.g, config->ring.b, config->ring.a);
	}
	if (options->ring_width_set) {
		matuwall_log_info("config", "ring width overridden to %u",
			config->ring_width);
	}
	if (options->spinner_set) {
		matuwall_log_info("config",
			"spinner overridden to #%02x%02x%02x%02x",
			config->spinner.r, config->spinner.g, config->spinner.b,
			config->spinner.a);
	}
	if (options->backend_set) {
		matuwall_log_info(
			"config", "backend overridden to %s", config->backend);
	}
	if (options->columns_set) {
		matuwall_log_info("config", "columns overridden to %u",
			config->layout.columns);
	}
	if (options->carousel_set) {
		matuwall_log_info("config", "carousel overridden to %s",
			config->carousel ? "true" : "false");
	}
	if (options->height_set) {
		matuwall_log_info("config", "thumbnail height overridden to %u",
			config->layout.tile_height);
	}
	if (options->margin_set) {
		matuwall_log_info("config", "margin overridden to %u",
			config->layout.margin);
	}
	if (options->edge_margin_set) {
		matuwall_log_info("config", "edge margin overridden to %u",
			config->edge_margin);
	}
	if (options->hooks_set) {
		matuwall_log_info("config",
			"on-apply hooks overridden with %zu command%s",
			options->hook_count,
			options->hook_count == 1 ? "" : "s");
	}
	if (options->panel_radius_set) {
		matuwall_log_info("config", "panel radius overridden to %u",
			config->panel_radius);
	}
	if (options->radius_set) {
		matuwall_log_info("config", "tile radius overridden to %u",
			config->layout.radius);
	}
	if (options->rows_set) {
		matuwall_log_info("config", "visible rows overridden to %u",
			config->visible_rows);
	}
	if (options->spacing_set) {
		matuwall_log_info("config", "spacing overridden to %u",
			config->layout.spacing);
	}
	if (options->width_set) {
		matuwall_log_info("config", "thumbnail width overridden to %u",
			config->layout.tile_width);
	}
	if (options->directory_set) {
		matuwall_log_info("config", "directory overridden to %s",
			config->directory);
	}
	if (options->position_set) {
		matuwall_log_info("config", "position overridden to %s",
			matuwall_position_name(config->position));
	}
	if (options->preview_set) {
		matuwall_log_info("config", "preview overridden to %s",
			config->preview ? "true" : "false");
	}
	if (options->close_on_focus_loss_set) {
		matuwall_log_info("config",
			"close on focus loss overridden to %s",
			config->close_on_focus_loss ? "true" : "false");
	}
}
