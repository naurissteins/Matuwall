#include "cli/options.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/backend.h"

enum {
	OPTION_CLEAR_CACHE = 256,
	OPTION_DIAGNOSE,
	OPTION_PREVIEW,
	OPTION_NO_PREVIEW,
};

enum parse_result {
	PARSE_CONTINUE,
	PARSE_DONE,
	PARSE_ERROR,
	PARSE_UNHANDLED,
};

static const struct option long_options[] = {
	{"backend", required_argument, NULL, 'b'},
	{"columns", required_argument, NULL, 'c'},
	{"directory", required_argument, NULL, 'd'},
	{"position", required_argument, NULL, 'p'},
	{"rows", required_argument, NULL, 'r'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{"clear-cache", no_argument, NULL, OPTION_CLEAR_CACHE},
	{"diagnose", no_argument, NULL, OPTION_DIAGNOSE},
	{"preview", no_argument, NULL, OPTION_PREVIEW},
	{"no-preview", no_argument, NULL, OPTION_NO_PREVIEW},
	{0},
};

void sweetwall_cli_usage(FILE *out) {
	fputs("usage: sweetwall [options]\n"
	      "\n"
	      "options:\n"
	      "  -b, --backend BACKEND\n"
	      "                     override backend: sweetbg, awww, or auto\n"
	      "  -c, --columns COLUMNS\n"
	      "                     override maximum grid columns (1..1024)\n"
	      "  -d, --directory DIRECTORY\n"
	      "                     override the wallpaper directory\n"
	      "  -p, --position POSITION\n"
	      "                     override position: center, left, right, "
	      "top, or bottom\n"
	      "  -r, --rows ROWS\n"
	      "                     override maximum visible rows (1..1024)\n"
	      "      --preview      enable full-screen live preview\n"
	      "      --no-preview   disable full-screen live preview\n"
	      "  -h, --help         show this help and exit\n"
	      "  -V, --version      show version information and exit\n"
	      "      --clear-cache  remove cached thumbnails and exit\n"
	      "      --diagnose     check the current setup and exit\n",
		out);
}

static bool parse_uint(
	const char *value, uint32_t min, uint32_t max, uint32_t *out) {
	if (value[0] < '0' || value[0] > '9') {
		return false;
	}

	errno = 0;
	char *end;
	unsigned long parsed = strtoul(value, &end, 10);
	if (errno == ERANGE || *end != '\0' || parsed < min || parsed > max) {
		return false;
	}
	*out = (uint32_t)parsed;
	return true;
}

static bool parse_backend(const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	if (!sweetwall_backend_name_valid(value)) {
		snprintf(err, err_size,
			"invalid backend '%s': expected sweetbg, awww, or auto",
			value);
		return false;
	}
	memcpy(options->backend, value, strlen(value) + 1);
	options->backend_set = true;
	return true;
}

static bool parse_columns(const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	if (!parse_uint(value, 1, 1024, &options->columns)) {
		snprintf(err, err_size,
			"invalid columns '%s': expected an integer from 1 to "
			"1024",
			value);
		return false;
	}
	options->columns_set = true;
	return true;
}

static bool parse_directory(const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	if (!sweetwall_config_expand_path(
		    value, options->directory, sizeof(options->directory))) {
		snprintf(err, err_size,
			"invalid directory '%s': path is empty, too long, or "
			"HOME is unset",
			value);
		return false;
	}
	options->directory_set = true;
	return true;
}

static bool parse_position(const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	if (!sweetwall_position_from_name(value, &options->position)) {
		snprintf(err, err_size,
			"invalid position '%s': expected center, left, right, "
			"top, or bottom",
			value);
		return false;
	}
	options->position_set = true;
	return true;
}

static bool parse_rows(const char *value, struct sweetwall_cli_options *options,
	char *err, size_t err_size) {
	if (!parse_uint(value, 1, 1024, &options->rows)) {
		snprintf(err, err_size,
			"invalid rows '%s': expected an integer from 1 to 1024",
			value);
		return false;
	}
	options->rows_set = true;
	return true;
}

static enum parse_result parse_override(int option, const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	switch (option) {
	case 'b':
		return parse_backend(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case 'c':
		return parse_columns(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case 'd':
		return parse_directory(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case 'p':
		return parse_position(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case 'r':
		return parse_rows(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case OPTION_PREVIEW:
		options->preview_set = true;
		options->preview = true;
		return PARSE_CONTINUE;
	case OPTION_NO_PREVIEW:
		options->preview_set = true;
		options->preview = false;
		return PARSE_CONTINUE;
	default:
		return PARSE_UNHANDLED;
	}
}

static enum parse_result parse_control(int option, const char *token,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	switch (option) {
	case 'h':
		options->action = SWEETWALL_CLI_HELP;
		return PARSE_DONE;
	case 'V':
		options->action = SWEETWALL_CLI_VERSION;
		return PARSE_DONE;
	case OPTION_CLEAR_CACHE:
		snprintf(err, err_size, "--clear-cache must be used alone");
		return PARSE_ERROR;
	case OPTION_DIAGNOSE:
		snprintf(err, err_size, "--diagnose must be used alone");
		return PARSE_ERROR;
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

bool sweetwall_cli_parse(int argc, char *argv[],
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	*options = (struct sweetwall_cli_options){0};

	if (argc == 2 && strcmp(argv[1], "--clear-cache") == 0) {
		options->action = SWEETWALL_CLI_CLEAR_CACHE;
		return true;
	}
	if (argc == 2 && strcmp(argv[1], "--diagnose") == 0) {
		options->action = SWEETWALL_CLI_DIAGNOSE;
		return true;
	}

	opterr = 0;
	optind = 1;
	for (;;) {
		int option = getopt_long(
			argc, argv, ":b:c:d:p:r:hV", long_options, NULL);
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
			option, argv[optind - 1], options, err, err_size);
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

void sweetwall_cli_apply(const struct sweetwall_cli_options *options,
	struct sweetwall_config *config) {
	if (options->backend_set) {
		memcpy(config->backend, options->backend,
			strlen(options->backend) + 1);
	}
	if (options->columns_set) {
		config->layout.columns = options->columns;
	}
	if (options->rows_set) {
		config->visible_rows = options->rows;
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
}
