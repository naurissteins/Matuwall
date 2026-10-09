#include "cli/options.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/backend.h"

enum {
	OPTION_CLEAR_CACHE = 256,
	OPTION_DIAGNOSE,
	OPTION_BACKEND_ARG,
	OPTION_BACKEND_COMMAND,
	OPTION_CONFIG,
	OPTION_HOOK,
	OPTION_NO_BACKEND_ARGS,
	OPTION_NO_CONFIG,
	OPTION_NO_HOOKS,
	OPTION_PRINT_CONFIG,
	// key table rows from here, two each: the flag and its off flag
	OPTION_KEY,
};

enum parse_result {
	PARSE_CONTINUE,
	PARSE_DONE,
	PARSE_ERROR,
	PARSE_UNHANDLED,
};

static const struct option fixed_options[] = {
	{"backend-arg", required_argument, NULL, OPTION_BACKEND_ARG},
	{"backend-command", required_argument, NULL, OPTION_BACKEND_COMMAND},
	{"config", required_argument, NULL, OPTION_CONFIG},
	{"hook", required_argument, NULL, OPTION_HOOK},
	{"no-backend-args", no_argument, NULL, OPTION_NO_BACKEND_ARGS},
	{"no-config", no_argument, NULL, OPTION_NO_CONFIG},
	{"no-hooks", no_argument, NULL, OPTION_NO_HOOKS},
	{"output", required_argument, NULL, 'o'},
	{"print-config", no_argument, NULL, OPTION_PRINT_CONFIG},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{"clear-cache", no_argument, NULL, OPTION_CLEAR_CACHE},
	{"diagnose", no_argument, NULL, OPTION_DIAGNOSE},
};

#define FIXED_OPTIONS (sizeof(fixed_options) / sizeof(fixed_options[0]))
#define MAX_OPTIONS (FIXED_OPTIONS + 2 * (size_t)MATUWALL_CONFIG_MAX_KEYS + 1)
#define MAX_SHORTS (2 * (size_t)MATUWALL_CONFIG_MAX_KEYS + 6)

// a key's long and short flag return the short letter, like getopt itself
static void build_options(struct option *options, char *shorts) {
	size_t n = 0;
	for (; n < FIXED_OPTIONS; n++) {
		options[n] = fixed_options[n];
	}
	*shorts++ = ':';
	for (size_t i = 0; i < matuwall_config_key_count; i++) {
		const struct matuwall_config_key *key =
			&matuwall_config_keys[i];
		if (key->flag == NULL) {
			continue;
		}
		int value = OPTION_KEY + (int)i * 2;
		if (key->short_flag != 0) {
			value = (unsigned char)key->short_flag;
			*shorts++ = key->short_flag;
			*shorts++ = ':';
		}
		options[n++] = (struct option){key->flag,
			key->type == MATUWALL_CONFIG_BOOL ? no_argument
							  : required_argument,
			NULL, value};
		if (key->off_flag != NULL) {
			options[n++] = (struct option){key->off_flag,
				no_argument, NULL, OPTION_KEY + (int)i * 2 + 1};
		}
	}
	memcpy(shorts, "o:hV", sizeof("o:hV"));
	options[n] = (struct option){0};
}

static bool key_option(int option, size_t *row, bool *off) {
	if (option >= OPTION_KEY) {
		*row = (size_t)(option - OPTION_KEY) / 2;
		*off = (option - OPTION_KEY) % 2 != 0;
		return *row < matuwall_config_key_count;
	}
	for (size_t i = 0; i < matuwall_config_key_count; i++) {
		if ((unsigned char)matuwall_config_keys[i].short_flag ==
			option) {
			*row = i;
			*off = false;
			return true;
		}
	}
	return false;
}

// validated now so a bad value exits before any config is read
static enum parse_result parse_key(size_t row, bool off, const char *value,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	const struct matuwall_config_key *key = &matuwall_config_keys[row];
	if (key->type == MATUWALL_CONFIG_BOOL) {
		value = off ? "false" : "true";
	} else if (!matuwall_config_set_text(NULL, key, value) ||
		   (key->type == MATUWALL_CONFIG_BACKEND &&
			   !matuwall_backend_name_valid(value))) {
		char expect[32];
		snprintf(err, err_size, "invalid --%s '%s': expected %s",
			key->flag, value,
			matuwall_config_expect(key, expect, sizeof(expect)));
		return PARSE_ERROR;
	}
	options->values[row] = value;
	return PARSE_CONTINUE;
}

static bool parse_path_override(const char *name, const char *value, char *out,
	size_t out_size, bool *is_set, char *err, size_t err_size) {
	if (!matuwall_config_expand_path(value, out, out_size)) {
		snprintf(err, err_size,
			"invalid %s '%s': path is empty, too long, or "
			"cannot be resolved",
			name, value);
		return false;
	}
	*is_set = true;
	return true;
}

static enum parse_result parse_hook(const char *value,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	size_t length = strlen(value);
	if (length == 0 || length >= MATUWALL_HOOK_MAX) {
		snprintf(err, err_size,
			"invalid hook: command must contain 1 to %d bytes",
			MATUWALL_HOOK_MAX - 1);
		return PARSE_ERROR;
	}
	if (!options->hooks_set) {
		options->hooks_set = true;
		options->hook_count = 0;
	}
	if (options->hook_count >= MATUWALL_MAX_HOOKS) {
		snprintf(err, err_size,
			"too many --hook options: maximum is %d",
			MATUWALL_MAX_HOOKS);
		return PARSE_ERROR;
	}
	options->hooks[options->hook_count++] = value;
	return PARSE_CONTINUE;
}

// first one replaces the configured args, later ones append
static enum parse_result parse_backend_arg(const char *value,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	size_t length = strlen(value);
	if (length == 0 || length >= MATUWALL_BACKEND_ARG_MAX) {
		snprintf(err, err_size,
			"invalid backend arg: must contain 1 to %d bytes",
			MATUWALL_BACKEND_ARG_MAX - 1);
		return PARSE_ERROR;
	}
	if (!options->backend_args_set) {
		options->backend_args_set = true;
		options->backend_arg_count = 0;
	}
	if (options->backend_arg_count >= MATUWALL_MAX_BACKEND_ARGS) {
		snprintf(err, err_size,
			"too many --backend-arg options: maximum is %d",
			MATUWALL_MAX_BACKEND_ARGS);
		return PARSE_ERROR;
	}
	options->backend_args[options->backend_arg_count++] = value;
	return PARSE_CONTINUE;
}

// same rules as [backend.command] apply, but a bad value is fatal here
static enum parse_result parse_backend_command(const char *value,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	size_t length = strlen(value);
	if (length == 0 || length >= MATUWALL_BACKEND_COMMAND_MAX) {
		snprintf(err, err_size,
			"invalid backend command: must contain 1 to %d bytes",
			MATUWALL_BACKEND_COMMAND_MAX - 1);
		return PARSE_ERROR;
	}
	if (strstr(value, "{path}") == NULL) {
		snprintf(err, err_size,
			"invalid backend command: must contain {path}");
		return PARSE_ERROR;
	}
	options->backend_command = value;
	return PARSE_CONTINUE;
}

static enum parse_result parse_override(int option, const char *value,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	size_t row;
	bool off;
	if (key_option(option, &row, &off)) {
		return parse_key(row, off, value, options, err, err_size);
	}

	switch (option) {
	case OPTION_BACKEND_ARG:
		return parse_backend_arg(value, options, err, err_size);
	case OPTION_BACKEND_COMMAND:
		return parse_backend_command(value, options, err, err_size);
	case OPTION_NO_BACKEND_ARGS:
		options->backend_args_set = true;
		options->backend_arg_count = 0;
		return PARSE_CONTINUE;
	case OPTION_CONFIG:
		if (!parse_path_override("config path", value,
			    options->config_path, sizeof(options->config_path),
			    &options->config_path_set, err, err_size)) {
			return PARSE_ERROR;
		}
		options->no_config = false;
		return PARSE_CONTINUE;
	case OPTION_HOOK:
		return parse_hook(value, options, err, err_size);
	case OPTION_NO_HOOKS:
		options->hooks_set = true;
		options->hook_count = 0;
		return PARSE_CONTINUE;
	case OPTION_NO_CONFIG:
		options->no_config = true;
		options->config_path_set = false;
		return PARSE_CONTINUE;
	case 'o':
		if (value[0] == '\0') {
			snprintf(err, err_size,
				"invalid output name: expected a non-empty "
				"name");
			return PARSE_ERROR;
		}
		options->output_name = value;
		return PARSE_CONTINUE;
	case OPTION_PRINT_CONFIG:
		options->action = MATUWALL_CLI_PRINT_CONFIG;
		return PARSE_CONTINUE;
	default:
		return PARSE_UNHANDLED;
	}
}

// cache and diagnose actions never mix with overrides, getopt_long also
// accepts their abbreviations, so argc is the only reliable "alone" test
static enum parse_result parse_alone(int option, int argc,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	bool clear = option == OPTION_CLEAR_CACHE;
	if (argc != 2) {
		snprintf(err, err_size, "%s must be used alone",
			clear ? "--clear-cache" : "--diagnose");
		return PARSE_ERROR;
	}
	options->action =
		clear ? MATUWALL_CLI_CLEAR_CACHE : MATUWALL_CLI_DIAGNOSE;
	return PARSE_DONE;
}

static enum parse_result parse_control(int option, int argc, const char *token,
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	switch (option) {
	case 'h':
		options->action = MATUWALL_CLI_HELP;
		return PARSE_DONE;
	case 'V':
		options->action = MATUWALL_CLI_VERSION;
		return PARSE_DONE;
	case OPTION_CLEAR_CACHE:
	case OPTION_DIAGNOSE:
		return parse_alone(option, argc, options, err, err_size);
	case ':':
		snprintf(err, err_size, "option '%s' requires a value", token);
		return PARSE_ERROR;
	case '?':
		snprintf(err, err_size, "unknown option '%s'", token);
		return PARSE_ERROR;
	default:
		snprintf(err, err_size, "invalid command line");
		return PARSE_ERROR;
	}
}

bool matuwall_cli_parse(int argc, char *argv[],
	struct matuwall_cli_options *options, char *err, size_t err_size) {
	*options = (struct matuwall_cli_options){0};

	struct option long_options[MAX_OPTIONS];
	char short_options[MAX_SHORTS];
	build_options(long_options, short_options);

	opterr = 0;
	optind = 1;
	for (;;) {
		int option = getopt_long(
			argc, argv, short_options, long_options, NULL);
		if (option == -1) {
			break;
		}

		enum parse_result result =
			parse_override(option, optarg, options, err, err_size);
		if (result == PARSE_ERROR) {
			return false;
		}
		if (result == PARSE_CONTINUE) {
			continue;
		}

		result = parse_control(
			option, argc, argv[optind - 1], options, err, err_size);
		if (result == PARSE_ERROR) {
			return false;
		}
		if (result == PARSE_DONE) {
			return true;
		}
	}

	if (optind < argc) {
		snprintf(err, err_size, "unexpected argument '%s'",
			argv[optind]);
		return false;
	}
	return true;
}
