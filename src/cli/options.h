#ifndef MATUWALL_CLI_OPTIONS_H
#define MATUWALL_CLI_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "config/config.h"

enum matuwall_cli_action {
	MATUWALL_CLI_RUN,
	MATUWALL_CLI_HELP,
	MATUWALL_CLI_VERSION,
	MATUWALL_CLI_CLEAR_CACHE,
	MATUWALL_CLI_DIAGNOSE,
	MATUWALL_CLI_PRINT_CONFIG,
};

struct matuwall_cli_options {
	enum matuwall_cli_action action;
	bool background_set;
	struct matuwall_color background;
	bool tile_set;
	struct matuwall_color tile;
	bool border_set;
	struct matuwall_color border;
	bool border_width_set;
	uint32_t border_width;
	bool shadow_set;
	struct matuwall_color shadow;
	bool shadow_width_set;
	uint32_t shadow_width;
	bool ring_set;
	struct matuwall_color ring;
	bool ring_width_set;
	uint32_t ring_width;
	bool spinner_set;
	struct matuwall_color spinner;
	bool backend_set;
	char backend[32];
	bool backend_args_set;
	// backend arg pointers borrow argv storage
	const char *backend_args[MATUWALL_MAX_BACKEND_ARGS];
	size_t backend_arg_count;
	bool columns_set;
	uint32_t columns;
	bool carousel_set;
	bool carousel;
	bool config_path_set;
	char config_path[PATH_MAX];
	bool no_config;
	bool height_set;
	uint32_t height;
	bool hooks_set;
	// Hook pointers borrow argv storage
	const char *hooks[MATUWALL_MAX_HOOKS];
	size_t hook_count;
	bool margin_set;
	uint32_t margin;
	bool edge_set;
	enum matuwall_edge edge;
	bool edge_margin_set;
	uint32_t edge_margin;
	bool output_set;
	// Output name borrows argv storage
	const char *output_name;
	bool panel_radius_set;
	uint32_t panel_radius;
	bool radius_set;
	uint32_t radius;
	bool rows_set;
	uint32_t rows;
	bool spacing_set;
	uint32_t spacing;
	bool width_set;
	uint32_t width;
	bool directory_set;
	char directory[PATH_MAX];
	bool position_set;
	enum matuwall_position position;
	bool preview_set;
	bool preview;
	bool close_on_focus_loss_set;
	bool close_on_focus_loss;
};

bool matuwall_cli_parse(int argc, char *argv[],
	struct matuwall_cli_options *options, char *err, size_t err_size);
void matuwall_cli_apply(const struct matuwall_cli_options *options,
	struct matuwall_config *config);
void matuwall_cli_log_overrides(const struct matuwall_cli_options *options,
	const struct matuwall_config *config);
void matuwall_cli_usage(FILE *out);

#endif
