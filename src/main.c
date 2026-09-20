#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "cli/options.h"
#include "config/print.h"
#include "diagnose/diagnose.h"
#include "thumb/cache.h"
#include "util/log.h"

static int clear_cache(void) {
	size_t removed;
	char err[640];
	if (!matuwall_cache_clear(&removed, err, sizeof(err))) {
		fprintf(stderr, "matuwall: failed to clear cache: %s\n", err);
		return 1;
	}
	if (removed == 0) {
		puts("matuwall: thumbnail cache is already empty");
	} else {
		printf("matuwall: removed %zu cached thumbnail%s\n", removed,
			removed == 1 ? "" : "s");
	}
	return 0;
}

static bool load_config(const struct matuwall_cli_options *options,
	struct matuwall_config *config, char *err, size_t err_size) {
	if (options->no_config) {
		matuwall_config_defaults(config);
		matuwall_log_info("config", "loading disabled; using defaults");
		return true;
	}
	if (options->config_path_set) {
		if (matuwall_config_load_path(
			    config, options->config_path, err, err_size)) {
			return true;
		}
		matuwall_log_error("config", "%s", err);
		return false;
	}
	if (!matuwall_config_load(config, err, err_size)) {
		// A bad config is a warning, not a crash: run with defaults
		matuwall_log_warn("config", "%s; using defaults", err);
	}
	return true;
}

int main(int argc, char *argv[]) {
	struct matuwall_cli_options options;
	char err[256];
	if (!matuwall_cli_parse(argc, argv, &options, err, sizeof(err))) {
		fprintf(stderr, "matuwall: %s\n", err);
		matuwall_cli_usage(stderr);
		return 2;
	}
	if (options.action == MATUWALL_CLI_HELP) {
		matuwall_cli_usage(stdout);
		return 0;
	}
	if (options.action == MATUWALL_CLI_VERSION) {
		puts("matuwall " MATUWALL_VERSION);
		return 0;
	}
	if (options.action == MATUWALL_CLI_CLEAR_CACHE) {
		return clear_cache();
	}
	if (options.action == MATUWALL_CLI_DIAGNOSE) {
		return matuwall_diagnose_run();
	}

	matuwall_log_start(MATUWALL_VERSION);
	struct matuwall_config config;
	if (!load_config(&options, &config, err, sizeof(err))) {
		matuwall_log_finish();
		return 1;
	}
	matuwall_cli_apply(&options, &config);
	if (options.action == MATUWALL_CLI_PRINT_CONFIG) {
		bool printed = matuwall_config_print(stdout, &config) &&
			       fflush(stdout) == 0;
		if (!printed) {
			matuwall_log_error("config", "cannot write stdout: %s",
				strerror(errno));
		}
		matuwall_log_finish();
		return printed ? 0 : 1;
	}
	matuwall_cli_log_overrides(&options, &config);

	struct matuwall_app app;
	const char *output_name =
		options.output_set ? options.output_name : NULL;
	bool ok = matuwall_app_init(&app, &config, output_name) &&
		  matuwall_app_run(&app);
	matuwall_app_finish(&app);
	matuwall_log_info("exit", "status %s", ok ? "success" : "failure");
	matuwall_log_finish();

	return ok ? 0 : 1;
}
