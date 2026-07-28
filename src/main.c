#include <stdio.h>
#include <string.h>

#include "app.h"

static void usage(FILE *out) {
	fputs("usage: sweetwall [options]\n"
	      "\n"
	      "options:\n"
	      "  -h, --help     show this help and exit\n"
	      "  -V, --version  show version and exit\n",
		out);
}

int main(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			usage(stdout);
			return 0;
		}

		if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
			puts("sweetwall " SWEETWALL_VERSION);
			return 0;
		}

		fprintf(stderr, "sweetwall: unknown option '%s'\n", arg);
		usage(stderr);
		return 2;
	}

	struct sweetwall_config config;
	char err[256];
	if (!sweetwall_config_load(&config, err, sizeof(err))) {
		// A bad config is a warning, not a crash: run with defaults
		fprintf(stderr, "sweetwall: %s\n", err);
	}

	struct sweetwall_app app;
	bool ok = sweetwall_app_init(&app, &config) && sweetwall_app_run(&app);
	sweetwall_app_finish(&app);

	return ok ? 0 : 1;
}
