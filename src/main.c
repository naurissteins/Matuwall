#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "thumb/cache.h"
#include "util/log.h"

static void usage(FILE *out) {
	fputs("usage: sweetwall [options]\n"
	      "\n"
	      "options:\n"
	      "  -h, --help         show this help and exit\n"
	      "  -V, --version      show version information and exit\n"
	      "      --clear-cache  remove cached thumbnails and exit\n",
		out);
}

static int clear_cache(void) {
	size_t removed;
	char err[640];
	if (!sweetwall_cache_clear(&removed, err, sizeof(err))) {
		fprintf(stderr, "sweetwall: failed to clear cache: %s\n", err);
		return 1;
	}
	if (removed == 0) {
		puts("sweetwall: thumbnail cache is already empty");
	} else {
		printf("sweetwall: removed %zu cached thumbnail%s\n", removed,
			removed == 1 ? "" : "s");
	}
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc == 2 && strcmp(argv[1], "--clear-cache") == 0) {
		return clear_cache();
	}

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (strcmp(arg, "--clear-cache") == 0) {
			fprintf(stderr, "sweetwall: --clear-cache must be used "
					"alone\n");
			return 2;
		}

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

	sweetwall_log_start(SWEETWALL_VERSION);
	struct sweetwall_config config;
	char err[256];
	if (!sweetwall_config_load(&config, err, sizeof(err))) {
		// A bad config is a warning, not a crash: run with defaults
		sweetwall_log_warn("config", "%s; using defaults", err);
	}

	struct sweetwall_app app;
	bool ok = sweetwall_app_init(&app, &config) && sweetwall_app_run(&app);
	sweetwall_app_finish(&app);
	sweetwall_log_info("exit", "status %s", ok ? "success" : "failure");
	sweetwall_log_finish();

	return ok ? 0 : 1;
}
