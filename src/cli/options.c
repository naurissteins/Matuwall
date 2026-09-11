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
	OPTION_BACKGROUND,
	OPTION_CONFIG,
	OPTION_HEIGHT,
	OPTION_HOOK,
	OPTION_NO_CONFIG,
	OPTION_NO_HOOKS,
	OPTION_PRINT_CONFIG,
	OPTION_RADIUS,
	OPTION_RING,
	OPTION_SPINNER,
	OPTION_TILE,
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
	{"background", required_argument, NULL, OPTION_BACKGROUND},
	{"backend", required_argument, NULL, 'b'},
	{"columns", required_argument, NULL, 'c'},
	{"config", required_argument, NULL, OPTION_CONFIG},
	{"directory", required_argument, NULL, 'd'},
	{"height", required_argument, NULL, OPTION_HEIGHT},
	{"hook", required_argument, NULL, OPTION_HOOK},
	{"margin", required_argument, NULL, 'm'},
	{"no-config", no_argument, NULL, OPTION_NO_CONFIG},
	{"no-hooks", no_argument, NULL, OPTION_NO_HOOKS},
	{"position", required_argument, NULL, 'p'},
	{"print-config", no_argument, NULL, OPTION_PRINT_CONFIG},
	{"radius", required_argument, NULL, OPTION_RADIUS},
	{"ring", required_argument, NULL, OPTION_RING},
	{"rows", required_argument, NULL, 'r'},
	{"spacing", required_argument, NULL, 's'},
	{"spinner", required_argument, NULL, OPTION_SPINNER},
	{"tile", required_argument, NULL, OPTION_TILE},
	{"width", required_argument, NULL, 'w'},
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
	      "      --background COLOR\n"
	      "                     override background: #rrggbb or #rrggbbaa\n"
	      "  -b, --backend BACKEND\n"
	      "                     override backend: sweetbg, awww, or auto\n"
	      "  -c, --columns COLUMNS\n"
	      "                     override maximum grid columns (1..1024)\n"
	      "      --config PATH  load an alternate configuration file\n"
	      "  -d, --directory DIRECTORY\n"
	      "                     override the wallpaper directory\n"
	      "      --height HEIGHT\n"
	      "                     override thumbnail height (1..16384)\n"
	      "      --hook COMMAND replace/append a temporary on-apply hook\n"
	      "  -m, --margin MARGIN\n"
	      "                     override window margin (0..4096)\n"
	      "      --no-config    use built-in defaults without loading "
	      "config\n"
	      "      --no-hooks     disable configured on-apply hooks\n"
	      "  -p, --position POSITION\n"
	      "                     override position: center, left, right, "
	      "top, or bottom\n"
	      "      --print-config print the resolved config and exit\n"
	      "      --radius RADIUS\n"
	      "                     override grid corner radius (0..4096)\n"
	      "      --ring COLOR\n"
	      "                     override ring color: #rrggbb or #rrggbbaa\n"
	      "  -r, --rows ROWS\n"
	      "                     override maximum visible rows (1..1024)\n"
	      "  -s, --spacing SPACING\n"
	      "                     override grid spacing (0..4096)\n"
	      "      --spinner COLOR\n"
	      "                     override spinner color: #rrggbb or "
	      "#rrggbbaa\n"
	      "      --tile COLOR\n"
	      "                     override tile color: #rrggbb or #rrggbbaa\n"
	      "  -w, --width WIDTH\n"
	      "                     override thumbnail width (1..16384)\n"
	      "      --preview      enable full-screen live preview\n"
	      "      --no-preview   disable full-screen live preview\n"
	      "  -h, --help         show this help and exit\n"
	      "  -V, --version      show version information and exit\n"
	      "      --clear-cache  remove cached thumbnails and exit\n"
	      "      --diagnose     check the current setup and exit\n",
		out);
}

static enum parse_result parse_uint_override(const char *name,
	const char *value, uint32_t min, uint32_t max, uint32_t *out,
	bool *is_set, char *err, size_t err_size) {
	errno = 0;
	char *end;
	unsigned long parsed = strtoul(value, &end, 10);
	if (value[0] < '0' || value[0] > '9' || errno == ERANGE ||
		*end != '\0' || parsed < min || parsed > max) {
		snprintf(err, err_size,
			"invalid %s '%s': expected an integer from %u to %u",
			name, value, min, max);
		return PARSE_ERROR;
	}
	*out = (uint32_t)parsed;
	*is_set = true;
	return PARSE_CONTINUE;
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

static enum parse_result parse_color_override(const char *name,
	const char *value, struct sweetwall_color *out, bool *is_set, char *err,
	size_t err_size) {
	if (!sweetwall_color_parse(value, out)) {
		snprintf(err, err_size,
			"invalid %s '%s': expected #rrggbb or #rrggbbaa", name,
			value);
		return PARSE_ERROR;
	}
	*is_set = true;
	return PARSE_CONTINUE;
}

static bool parse_path_override(const char *name, const char *value, char *out,
	size_t out_size, bool *is_set, char *err, size_t err_size) {
	if (!sweetwall_config_expand_path(value, out, out_size)) {
		snprintf(err, err_size,
			"invalid %s '%s': path is empty, too long, or "
			"HOME is unset",
			name, value);
		return false;
	}
	*is_set = true;
	return true;
}

static enum parse_result parse_hook(const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	size_t length = strlen(value);
	if (length == 0 || length >= SWEETWALL_HOOK_MAX) {
		snprintf(err, err_size,
			"invalid hook: command must contain 1 to %d bytes",
			SWEETWALL_HOOK_MAX - 1);
		return PARSE_ERROR;
	}
	if (!options->hooks_set) {
		options->hooks_set = true;
		options->hook_count = 0;
	}
	if (options->hook_count >= SWEETWALL_MAX_HOOKS) {
		snprintf(err, err_size,
			"too many --hook options: maximum is %d",
			SWEETWALL_MAX_HOOKS);
		return PARSE_ERROR;
	}
	options->hooks[options->hook_count++] = value;
	return PARSE_CONTINUE;
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

static enum parse_result parse_numeric_override(int option, const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	switch (option) {
	case 'c':
		return parse_uint_override("columns", value, 1, 1024,
			&options->columns, &options->columns_set, err,
			err_size);
	case OPTION_HEIGHT:
		return parse_uint_override("height", value, 1, 16384,
			&options->height, &options->height_set, err, err_size);
	case 'm':
		return parse_uint_override("margin", value, 0, 4096,
			&options->margin, &options->margin_set, err, err_size);
	case OPTION_RADIUS:
		return parse_uint_override("radius", value, 0, 4096,
			&options->radius, &options->radius_set, err, err_size);
	case 'r':
		return parse_uint_override("rows", value, 1, 1024,
			&options->rows, &options->rows_set, err, err_size);
	case 's':
		return parse_uint_override("spacing", value, 0, 4096,
			&options->spacing, &options->spacing_set, err,
			err_size);
	case 'w':
		return parse_uint_override("width", value, 1, 16384,
			&options->width, &options->width_set, err, err_size);
	default:
		return PARSE_UNHANDLED;
	}
}

static enum parse_result parse_override(int option, const char *value,
	struct sweetwall_cli_options *options, char *err, size_t err_size) {
	enum parse_result result =
		parse_numeric_override(option, value, options, err, err_size);
	if (result != PARSE_UNHANDLED) {
		return result;
	}

	switch (option) {
	case OPTION_BACKGROUND:
		return parse_color_override("background", value,
			&options->background, &options->background_set, err,
			err_size);
	case 'b':
		return parse_backend(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case OPTION_CONFIG:
		if (!parse_path_override("config path", value,
			    options->config_path, sizeof(options->config_path),
			    &options->config_path_set, err, err_size)) {
			return PARSE_ERROR;
		}
		options->no_config = false;
		return PARSE_CONTINUE;
	case 'd':
		return parse_path_override("directory", value,
			       options->directory, sizeof(options->directory),
			       &options->directory_set, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case OPTION_HOOK:
		return parse_hook(value, options, err, err_size);
	case 'p':
		return parse_position(value, options, err, err_size)
			       ? PARSE_CONTINUE
			       : PARSE_ERROR;
	case OPTION_NO_HOOKS:
		options->hooks_set = true;
		options->hook_count = 0;
		return PARSE_CONTINUE;
	case OPTION_NO_CONFIG:
		options->no_config = true;
		options->config_path_set = false;
		return PARSE_CONTINUE;
	case OPTION_PRINT_CONFIG:
		options->action = SWEETWALL_CLI_PRINT_CONFIG;
		return PARSE_CONTINUE;
	case OPTION_RING:
		return parse_color_override("ring", value, &options->ring,
			&options->ring_set, err, err_size);
	case OPTION_SPINNER:
		return parse_color_override("spinner", value, &options->spinner,
			&options->spinner_set, err, err_size);
	case OPTION_TILE:
		return parse_color_override("tile", value, &options->tile,
			&options->tile_set, err, err_size);
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
			argc, argv, ":b:c:d:m:p:r:s:w:hV", long_options, NULL);
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
