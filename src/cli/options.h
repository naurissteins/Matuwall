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
	// every pointer borrows argv storage
	// by key table row, NULL when not given
	const char *values[MATUWALL_CONFIG_MAX_KEYS];
	bool backend_args_set;
	const char *backend_args[MATUWALL_MAX_BACKEND_ARGS];
	size_t backend_arg_count;
	const char *backend_command;
	bool config_path_set;
	char config_path[PATH_MAX];
	bool no_config;
	bool hooks_set;
	const char *hooks[MATUWALL_MAX_HOOKS];
	size_t hook_count;
	const char *output_name;
};

bool matuwall_cli_parse(int argc, char *argv[],
	struct matuwall_cli_options *options, char *err, size_t err_size);
void matuwall_cli_apply(const struct matuwall_cli_options *options,
	struct matuwall_config *config);
void matuwall_cli_log_overrides(const struct matuwall_cli_options *options,
	const struct matuwall_config *config);
void matuwall_cli_usage(FILE *out);

#endif
