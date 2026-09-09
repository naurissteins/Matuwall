#include <stdio.h>

#include "app/app.h"
#include "cli/options.h"
#include "diagnose/diagnose.h"
#include "thumb/cache.h"
#include "util/log.h"

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
	struct sweetwall_cli_options options;
	char err[256];
	if (!sweetwall_cli_parse(argc, argv, &options, err, sizeof(err))) {
		fprintf(stderr, "sweetwall: %s\n", err);
		sweetwall_cli_usage(stderr);
		return 2;
	}
	if (options.action == SWEETWALL_CLI_HELP) {
		sweetwall_cli_usage(stdout);
		return 0;
	}
	if (options.action == SWEETWALL_CLI_VERSION) {
		puts("sweetwall " SWEETWALL_VERSION);
		return 0;
	}
	if (options.action == SWEETWALL_CLI_CLEAR_CACHE) {
		return clear_cache();
	}
	if (options.action == SWEETWALL_CLI_DIAGNOSE) {
		return sweetwall_diagnose_run();
	}

	sweetwall_log_start(SWEETWALL_VERSION);
	struct sweetwall_config config;
	if (!sweetwall_config_load(&config, err, sizeof(err))) {
		// A bad config is a warning, not a crash: run with defaults
		sweetwall_log_warn("config", "%s; using defaults", err);
	}
	sweetwall_cli_apply(&options, &config);
	if (options.backend_set) {
		sweetwall_log_info(
			"config", "backend overridden to %s", config.backend);
	}
	if (options.columns_set) {
		sweetwall_log_info("config", "columns overridden to %u",
			config.layout.columns);
	}
	if (options.directory_set) {
		sweetwall_log_info("config", "directory overridden to %s",
			config.directory);
	}
	if (options.position_set) {
		sweetwall_log_info("config", "position overridden to %s",
			sweetwall_position_name(config.position));
	}
	if (options.preview_set) {
		sweetwall_log_info("config", "preview overridden to %s",
			config.preview ? "true" : "false");
	}

	struct sweetwall_app app;
	bool ok = sweetwall_app_init(&app, &config) && sweetwall_app_run(&app);
	sweetwall_app_finish(&app);
	sweetwall_log_info("exit", "status %s", ok ? "success" : "failure");
	sweetwall_log_finish();

	return ok ? 0 : 1;
}
