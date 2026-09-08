#include "cli/options.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

enum {
	OPTION_CLEAR_CACHE = 256,
	OPTION_DIAGNOSE,
	OPTION_PREVIEW,
	OPTION_NO_PREVIEW,
};

void sweetwall_cli_usage(FILE *out) {
	fputs("usage: sweetwall [options]\n"
	      "\n"
	      "options:\n"
	      "  -p, --position POSITION\n"
	      "                     override position: center, left, right, "
	      "top, or bottom\n"
	      "      --preview      enable full-screen live preview\n"
	      "      --no-preview   disable full-screen live preview\n"
	      "  -h, --help         show this help and exit\n"
	      "  -V, --version      show version information and exit\n"
	      "      --clear-cache  remove cached thumbnails and exit\n"
	      "      --diagnose     check the current setup and exit\n",
		out);
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

	static const struct option long_options[] = {
		{"position", required_argument, NULL, 'p'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{"clear-cache", no_argument, NULL, OPTION_CLEAR_CACHE},
		{"diagnose", no_argument, NULL, OPTION_DIAGNOSE},
		{"preview", no_argument, NULL, OPTION_PREVIEW},
		{"no-preview", no_argument, NULL, OPTION_NO_PREVIEW},
		{0},
	};

	opterr = 0;
	optind = 1;
	for (;;) {
		int option =
			getopt_long(argc, argv, ":p:hV", long_options, NULL);
		if (option == -1) {
			break;
		}

		switch (option) {
		case 'p':
			if (!parse_position(optarg, options, err, err_size)) {
				return false;
			}
			break;
		case 'h':
			options->action = SWEETWALL_CLI_HELP;
			return true;
		case 'V':
			options->action = SWEETWALL_CLI_VERSION;
			return true;
		case OPTION_CLEAR_CACHE:
			snprintf(err, err_size,
				"--clear-cache must be used alone");
			return false;
		case OPTION_DIAGNOSE:
			snprintf(
				err, err_size, "--diagnose must be used alone");
			return false;
		case OPTION_PREVIEW:
			options->preview_set = true;
			options->preview = true;
			break;
		case OPTION_NO_PREVIEW:
			options->preview_set = true;
			options->preview = false;
			break;
		case ':':
			snprintf(err, err_size, "option '%s' requires a value",
				argv[optind - 1]);
			return false;
		case '?':
			snprintf(err, err_size, "unknown option '%s'",
				argv[optind - 1]);
			return false;
		default:
			snprintf(err, err_size, "invalid command line");
			return false;
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
	if (options->position_set) {
		config->position = options->position;
	}
	if (options->preview_set) {
		config->preview = options->preview;
	}
}
