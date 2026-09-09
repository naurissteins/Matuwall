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
};

struct sweetwall_cli_options {
	enum sweetwall_cli_action action;
	bool backend_set;
	char backend[32];
	bool columns_set;
	uint32_t columns;
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
