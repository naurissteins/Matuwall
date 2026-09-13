#include "diagnose/diagnose.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

#include "backend/backend.h"
#include "config/config.h"
#include "state/selection.h"
#include "thumb/cache.h"
#include "util/log.h"

enum diagnose_level {
	DIAG_INFO,
	DIAG_OK,
	DIAG_WARN,
	DIAG_ERROR,
};

struct diagnose_report {
	unsigned int warnings;
	unsigned int errors;
};

struct backend_probe {
	const struct matuwall_backend *backend;
	bool client;
	bool ready;
};

static const char *level_name(enum diagnose_level level) {
	switch (level) {
	case DIAG_OK:
		return "OK";
	case DIAG_WARN:
		return "WARN";
	case DIAG_ERROR:
		return "ERROR";
	case DIAG_INFO:
		break;
	}
	return "INFO";
}

static void report_line(struct diagnose_report *report,
	enum diagnose_level level, const char *subject, const char *format,
	...) {
	if (level == DIAG_WARN) {
		report->warnings++;
	} else if (level == DIAG_ERROR) {
		report->errors++;
	}
	printf("  %-5s %-18s ", level_name(level), subject);
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	putchar('\n');
}

static const char *clean(const char *input, char *out, size_t out_size) {
	if (out_size == 0) {
		return "";
	}
	size_t written = 0;
	for (const unsigned char *p = (const unsigned char *)input;
		*p != '\0' && written + 1 < out_size; p++) {
		out[written++] = *p < 0x20 || *p == 0x7f ? '?' : (char)*p;
	}
	out[written] = '\0';
	return out;
}

static void report_env(struct diagnose_report *report, const char *name,
	const char *fallback) {
	const char *value = getenv(name);
	if (value == NULL || value[0] == '\0') {
		report_line(report, DIAG_INFO, name, "unset (%s)", fallback);
		return;
	}
	char shown[PATH_MAX];
	report_line(report, DIAG_INFO, name, "%s",
		clean(value, shown, sizeof(shown)));
}

static void report_environment(struct diagnose_report *report) {
	puts("\nEnvironment");
	report_env(report, "HOME", "no home-directory fallback");
	report_env(report, "XDG_CONFIG_HOME", "using $HOME/.config");
	report_env(report, "XDG_STATE_HOME", "using $HOME/.local/state");
	report_env(report, "XDG_CACHE_HOME", "using $HOME/.cache");
	report_env(report, "XDG_RUNTIME_DIR", "required by Wayland");
	report_env(report, "WAYLAND_DISPLAY", "Wayland defaults to wayland-0");

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	if ((runtime == NULL || runtime[0] != '/') &&
		(display == NULL || display[0] != '/')) {
		report_line(report, DIAG_ERROR, "Wayland",
			"cannot resolve the compositor socket");
		return;
	}
	struct wl_display *connection = wl_display_connect(NULL);
	if (connection == NULL) {
		report_line(report, DIAG_ERROR, "Wayland",
			"compositor is not reachable");
		return;
	}
	wl_display_disconnect(connection);
	report_line(report, DIAG_OK, "Wayland", "compositor is reachable");
}

static void report_directory(
	struct diagnose_report *report, const char *directory) {
	if (directory[0] == '\0') {
		report_line(report, DIAG_ERROR, "wallpapers",
			"directory is unresolved (is HOME set?)");
		return;
	}
	DIR *dir = opendir(directory);
	if (dir == NULL) {
		char shown[PATH_MAX];
		report_line(report, DIAG_ERROR, "wallpapers", "%s: %s",
			clean(directory, shown, sizeof(shown)),
			strerror(errno));
		return;
	}
	if (closedir(dir) != 0) {
		char shown[PATH_MAX];
		report_line(report, DIAG_WARN, "wallpapers",
			"could not close %s cleanly: %s",
			clean(directory, shown, sizeof(shown)),
			strerror(errno));
		return;
	}
	char shown[PATH_MAX];
	report_line(report, DIAG_OK, "wallpapers", "%s is readable",
		clean(directory, shown, sizeof(shown)));
}

static void report_configuration(
	struct diagnose_report *report, struct matuwall_config *config) {
	puts("\nConfiguration");
	char path[PATH_MAX];
	bool have_path = matuwall_config_path(path, sizeof(path));
	struct stat info;
	if (!have_path) {
		report_line(report, DIAG_WARN, "config",
			"path is unresolved; using defaults");
	} else {
		char shown[PATH_MAX];
		const char *visible = clean(path, shown, sizeof(shown));
		if (stat(path, &info) == 0) {
			report_line(report,
				S_ISREG(info.st_mode) ? DIAG_OK : DIAG_WARN,
				"config", "%s%s", visible,
				S_ISREG(info.st_mode)
					? ""
					: " (not a regular file)");
		} else if (errno == ENOENT) {
			report_line(report, DIAG_INFO, "config",
				"%s not found; defaults", visible);
		} else {
			report_line(report, DIAG_WARN, "config", "%s: %s",
				visible, strerror(errno));
		}
	}

	char error[256] = {0};
	fflush(stdout);
	if (!matuwall_config_load(config, error, sizeof(error))) {
		char shown[sizeof(error)];
		report_line(report, DIAG_WARN, "config load", "%s; defaults",
			clean(error, shown, sizeof(shown)));
	}
	size_t warnings = matuwall_config_warning_count();
	if (warnings > 0) {
		report_line(report, DIAG_WARN, "config values",
			"%zu invalid value%s used defaults", warnings,
			warnings == 1 ? "" : "s");
	}
	char backend[sizeof(config->backend)];
	report_line(report, DIAG_INFO, "backend", "%s",
		clean(config->backend, backend, sizeof(backend)));
	report_line(report, DIAG_INFO, "grid", "maximum %ux%u, tile %ux%u",
		config->layout.columns, config->visible_rows,
		config->layout.tile_width, config->layout.tile_height);
	report_line(report, DIAG_INFO, "preview", "%s",
		config->preview ? "enabled" : "disabled");
	report_directory(report, config->directory);
}

static void report_backends(
	struct diagnose_report *report, const struct matuwall_config *config) {
	puts("\nBackends");
	struct backend_probe probes[] = {
		{.backend = &matuwall_backend_sweetbg},
		{.backend = &matuwall_backend_awww},
	};
	const struct backend_probe *configured = NULL;
	const struct backend_probe *automatic = NULL;
	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
		probes[i].client =
			matuwall_backend_available(probes[i].backend->name);
		probes[i].ready =
			probes[i].client && probes[i].backend->detect();
		const char *detail =
			probes[i].ready	   ? "client found, daemon detected"
			: probes[i].client ? "client found, daemon not detected"
					   : "client not found on PATH";
		report_line(report, DIAG_INFO, probes[i].backend->name, "%s",
			detail);
		if (strcmp(config->backend, probes[i].backend->name) == 0) {
			configured = &probes[i];
		}
		if (automatic == NULL && probes[i].ready) {
			automatic = &probes[i];
		}
	}

	if (strcmp(config->backend, "auto") == 0) {
		if (automatic != NULL) {
			report_line(report, DIAG_OK, "configured",
				"auto would select %s",
				automatic->backend->name);
		} else {
			report_line(report, DIAG_ERROR, "configured",
				"auto found no running backend");
		}
	} else if (configured == NULL) {
		char shown[sizeof(config->backend)];
		report_line(report, DIAG_ERROR, "configured",
			"unknown backend %s",
			clean(config->backend, shown, sizeof(shown)));
	} else if (!configured->client) {
		report_line(report, DIAG_ERROR, "configured",
			"%s is not on PATH", configured->backend->name);
	} else if (!configured->ready) {
		report_line(report, DIAG_WARN, "configured",
			"%s daemon not detected; explicit selection still runs",
			configured->backend->name);
	} else {
		report_line(report, DIAG_OK, "configured", "%s is ready",
			configured->backend->name);
	}
}

static bool command_name(const char *command, char *out, size_t out_size) {
	while (*command == ' ' || *command == '\t') {
		command++;
	}
	size_t length = 0;
	while (command[length] != '\0' && command[length] != ' ' &&
		command[length] != '\t') {
		length++;
	}
	if (length == 0 || length >= out_size) {
		return false;
	}
	memcpy(out, command, length);
	out[length] = '\0';
	return true;
}

static bool command_available(const char *command) {
	if (strchr(command, '/') != NULL) {
		return access(command, X_OK) == 0;
	}
	return matuwall_backend_available(command);
}

static void report_hooks(
	struct diagnose_report *report, const struct matuwall_config *config) {
	puts("\nHooks");
	if (config->on_apply_count == 0) {
		report_line(report, DIAG_INFO, "on_apply", "none configured");
		return;
	}
	for (size_t i = 0; i < config->on_apply_count; i++) {
		char executable[PATH_MAX];
		char subject[32];
		int length =
			snprintf(subject, sizeof(subject), "on_apply[%zu]", i);
		if (length < 0 || (size_t)length >= sizeof(subject)) {
			continue;
		}
		if (!command_name(config->on_apply[i], executable,
			    sizeof(executable))) {
			report_line(report, DIAG_WARN, subject,
				"empty or invalid command");
			continue;
		}
		char shown[PATH_MAX];
		bool available = command_available(executable);
		report_line(report, available ? DIAG_OK : DIAG_WARN, subject,
			"%s %s", clean(executable, shown, sizeof(shown)),
			available ? "found" : "not found");
	}
}

static void report_file(struct diagnose_report *report, const char *subject,
	const char *path, bool directory, int access_mode) {
	char shown[PATH_MAX];
	const char *visible = clean(path, shown, sizeof(shown));
	struct stat info;
	if (lstat(path, &info) != 0) {
		if (errno == ENOENT) {
			report_line(report, DIAG_INFO, subject,
				"%s (not created)", visible);
		} else {
			report_line(report, DIAG_WARN, subject, "%s: %s",
				visible, strerror(errno));
		}
		return;
	}
	bool type_ok =
		directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode);
	if (!type_ok) {
		report_line(report, DIAG_WARN, subject,
			"%s (unexpected file type)", visible);
		return;
	}
	bool accessible = access(path, access_mode) == 0;
	report_line(report, accessible ? DIAG_OK : DIAG_WARN, subject, "%s%s",
		visible, accessible ? "" : " (insufficient access)");
}

static void report_paths(struct diagnose_report *report) {
	puts("\nState and cache");
	char path[PATH_MAX];
	if (matuwall_cache_dir(path, sizeof(path))) {
		report_file(report, "thumbnail cache", path, true,
			R_OK | W_OK | X_OK);
	} else {
		report_line(report, DIAG_WARN, "thumbnail cache",
			"path is unresolved");
	}

	char state_path[PATH_MAX];
	if (matuwall_selection_path(state_path, sizeof(state_path))) {
		struct stat state_info;
		bool state_regular = lstat(state_path, &state_info) == 0 &&
				     S_ISREG(state_info.st_mode);
		report_file(report, "selection state", state_path, false, R_OK);
		char selected[PATH_MAX];
		if (matuwall_selection_load(selected, sizeof(selected))) {
			char shown[PATH_MAX];
			report_line(report, DIAG_INFO, "last selection", "%s",
				clean(selected, shown, sizeof(shown)));
		} else if (state_regular) {
			report_line(report, DIAG_WARN, "last selection",
				"state is corrupt or unreadable");
		}
	} else {
		report_line(report, DIAG_WARN, "selection state",
			"path is unresolved");
	}

	if (matuwall_log_path(path, sizeof(path))) {
		report_file(report, "diagnostic log", path, false, R_OK | W_OK);
	} else {
		report_line(report, DIAG_WARN, "diagnostic log",
			"path is unresolved");
	}
}

int matuwall_diagnose_run(void) {
	struct diagnose_report report = {0};
	printf("matuwall %s diagnostics\n", MATUWALL_VERSION);
	report_environment(&report);
	struct matuwall_config config;
	report_configuration(&report, &config);
	report_backends(&report, &config);
	report_hooks(&report, &config);
	report_paths(&report);

	puts("\nSummary");
	if (report.errors > 0) {
		printf("  %-5s %-18s %u error%s, %u warning%s\n",
			level_name(DIAG_ERROR), "result", report.errors,
			report.errors == 1 ? "" : "s", report.warnings,
			report.warnings == 1 ? "" : "s");
		return 1;
	}
	printf("  %-5s %-18s no errors, %u warning%s\n",
		level_name(report.warnings > 0 ? DIAG_WARN : DIAG_OK), "result",
		report.warnings, report.warnings == 1 ? "" : "s");
	return 0;
}
