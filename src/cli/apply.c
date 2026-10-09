#include "cli/options.h"

#include <string.h>

#include "util/log.h"

// One list for this run, whichever backend ends up applying it
static void apply_backend_args(const struct matuwall_cli_options *options,
	struct matuwall_backend_args *args) {
	args->count = options->backend_arg_count;
	for (size_t i = 0; i < options->backend_arg_count; i++) {
		memcpy(args->items[i], options->backend_args[i],
			strlen(options->backend_args[i]) + 1);
	}
}

static bool backend_given(const struct matuwall_cli_options *options) {
	for (size_t i = 0; i < matuwall_config_key_count; i++) {
		if (matuwall_config_keys[i].type == MATUWALL_CONFIG_BACKEND &&
			options->values[i] != NULL) {
			return true;
		}
	}
	return false;
}

void matuwall_cli_apply(const struct matuwall_cli_options *options,
	struct matuwall_config *config) {
	for (size_t i = 0; i < matuwall_config_key_count; i++) {
		// parsing already checked each value against the same setter
		if (options->values[i] != NULL) {
			(void)matuwall_config_set_text(config,
				&matuwall_config_keys[i], options->values[i]);
		}
	}
	if (options->backend_args_set) {
		apply_backend_args(options, &config->sweetbg_args);
		apply_backend_args(options, &config->awww_args);
		apply_backend_args(options, &config->plasma_args);
	}
	if (options->backend_command != NULL) {
		memcpy(config->backend_command, options->backend_command,
			strlen(options->backend_command) + 1);
		// a command for this run means using it, unless -b names
		// another
		if (!backend_given(options)) {
			memcpy(config->backend, "command", sizeof("command"));
		}
	}
	if (options->hooks_set) {
		config->on_apply_count = options->hook_count;
		for (size_t i = 0; i < options->hook_count; i++) {
			memcpy(config->on_apply[i], options->hooks[i],
				strlen(options->hooks[i]) + 1);
		}
	}
}

void matuwall_cli_log_overrides(const struct matuwall_cli_options *options,
	const struct matuwall_config *config) {
	for (size_t i = 0; i < matuwall_config_key_count; i++) {
		if (options->values[i] == NULL) {
			continue;
		}
		const struct matuwall_config_key *key =
			&matuwall_config_keys[i];
		char value[PATH_MAX];
		matuwall_config_format(config, key, value, sizeof(value));
		matuwall_log_info("config", "%s.%s overridden to %s",
			key->section, key->name, value);
	}
	if (options->backend_args_set) {
		matuwall_log_info("config",
			"backend args overridden with %zu arg%s",
			options->backend_arg_count,
			options->backend_arg_count == 1 ? "" : "s");
	}
	if (options->backend_command != NULL) {
		// the command itself may carry secrets, so it is not logged
		matuwall_log_info("config", "backend command overridden%s",
			backend_given(options) ? ""
					       : ", backend set to command");
	}
	if (options->hooks_set) {
		matuwall_log_info("config",
			"on-apply hooks overridden with %zu command%s",
			options->hook_count,
			options->hook_count == 1 ? "" : "s");
	}
}
