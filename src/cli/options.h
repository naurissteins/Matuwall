#ifndef SWEETWALL_CLI_OPTIONS_H
#define SWEETWALL_CLI_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "config/config.h"

enum sweetwall_cli_action {
	SWEETWALL_CLI_RUN,
	SWEETWALL_CLI_HELP,
	SWEETWALL_CLI_VERSION,
	SWEETWALL_CLI_CLEAR_CACHE,
	SWEETWALL_CLI_DIAGNOSE,
	SWEETWALL_CLI_PRINT_CONFIG,
};

struct sweetwall_cli_options {
	enum sweetwall_cli_action action;
	bool background_set;
	struct sweetwall_color background;
	bool tile_set;
	struct sweetwall_color tile;
	bool ring_set;
	struct sweetwall_color ring;
	bool ring_width_set;
	uint32_t ring_width;
	bool spinner_set;
	struct sweetwall_color spinner;
	bool backend_set;
	char backend[32];
	bool columns_set;
	uint32_t columns;
	bool config_path_set;
	char config_path[PATH_MAX];
	bool no_config;
	bool height_set;
	uint32_t height;
	bool hooks_set;
	// Hook pointers borrow argv storage
	const char *hooks[SWEETWALL_MAX_HOOKS];
	size_t hook_count;
	bool margin_set;
	uint32_t margin;
	bool output_set;
	// Output name borrows argv storage
	const char *output_name;
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
	enum sweetwall_position position;
	bool preview_set;
	bool preview;
};

bool sweetwall_cli_parse(int argc, char *argv[],
	struct sweetwall_cli_options *options, char *err, size_t err_size);
void sweetwall_cli_apply(const struct sweetwall_cli_options *options,
	struct sweetwall_config *config);
void sweetwall_cli_usage(FILE *out);

#endif
