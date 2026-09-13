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
	if (options.background_set) {
		matuwall_log_info("config",
			"background overridden to #%02x%02x%02x%02x",
			config.background.r, config.background.g,
			config.background.b, config.background.a);
	}
	if (options.tile_set) {
		matuwall_log_info("config",
			"tile overridden to #%02x%02x%02x%02x", config.tile.r,
			config.tile.g, config.tile.b, config.tile.a);
	}
	if (options.border_set) {
		matuwall_log_info("config",
			"border overridden to #%02x%02x%02x%02x",
			config.border.r, config.border.g, config.border.b,
			config.border.a);
	}
	if (options.border_width_set) {
		matuwall_log_info("config", "border width overridden to %u",
			config.border_width);
	}
	if (options.ring_set) {
		matuwall_log_info("config",
			"ring overridden to #%02x%02x%02x%02x", config.ring.r,
			config.ring.g, config.ring.b, config.ring.a);
	}
	if (options.ring_width_set) {
		matuwall_log_info("config", "ring width overridden to %u",
			config.ring_width);
	}
	if (options.spinner_set) {
		matuwall_log_info("config",
			"spinner overridden to #%02x%02x%02x%02x",
			config.spinner.r, config.spinner.g, config.spinner.b,
			config.spinner.a);
	}
	if (options.backend_set) {
		matuwall_log_info(
			"config", "backend overridden to %s", config.backend);
	}
	if (options.columns_set) {
		matuwall_log_info("config", "columns overridden to %u",
			config.layout.columns);
	}
	if (options.height_set) {
		matuwall_log_info("config", "thumbnail height overridden to %u",
			config.layout.tile_height);
	}
	if (options.margin_set) {
		matuwall_log_info("config", "margin overridden to %u",
			config.layout.margin);
	}
	if (options.hooks_set) {
		matuwall_log_info("config",
			"on-apply hooks overridden with %zu command%s",
			options.hook_count, options.hook_count == 1 ? "" : "s");
	}
	if (options.panel_radius_set) {
		matuwall_log_info("config", "panel radius overridden to %u",
			config.panel_radius);
	}
	if (options.radius_set) {
		matuwall_log_info("config", "tile radius overridden to %u",
			config.layout.radius);
	}
	if (options.rows_set) {
		matuwall_log_info("config", "visible rows overridden to %u",
			config.visible_rows);
	}
	if (options.spacing_set) {
		matuwall_log_info("config", "spacing overridden to %u",
			config.layout.spacing);
	}
	if (options.width_set) {
		matuwall_log_info("config", "thumbnail width overridden to %u",
			config.layout.tile_width);
	}
	if (options.directory_set) {
		matuwall_log_info("config", "directory overridden to %s",
			config.directory);
	}
	if (options.position_set) {
		matuwall_log_info("config", "position overridden to %s",
			matuwall_position_name(config.position));
	}
	if (options.preview_set) {
		matuwall_log_info("config", "preview overridden to %s",
			config.preview ? "true" : "false");
	}
	if (options.close_on_focus_loss_set) {
		matuwall_log_info("config",
			"close on focus loss overridden to %s",
			config.close_on_focus_loss ? "true" : "false");
	}

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
