#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backend/backend.h"
#include "util/log.h"

// KDE Plasma draws its own wallpaper, plasma-apply-wallpaperimage asks
// plasmashell over D-Bus to set it on every desktop, then exits

#define PLASMA_CLIENT "plasma-apply-wallpaperimage"

// XDG_CURRENT_DESKTOP is a colon-separated list, Plasma registers "KDE"
static bool plasma_session(void) {
	const char *desktops = getenv("XDG_CURRENT_DESKTOP");
	if (desktops == NULL) {
		return false;
	}
	// a plain walk, a strcspn offset trips clang-tidy's tainted bound check
	const char *token = desktops;
	for (const char *p = desktops;; p++) {
		if (*p != ':' && *p != '\0') {
			continue;
		}
		if (p - token == 3 && strncmp(token, "KDE", 3) == 0) {
			return true;
		}
		if (*p == '\0') {
			return false;
		}
		token = p + 1;
	}
}

// session check keeps auto off Plasma when only its tools are installed
static bool plasma_detect(void) {
	if (!matuwall_backend_available(PLASMA_CLIENT)) {
		matuwall_log_info("backend",
			"plasma skipped: " PLASMA_CLIENT " not found on PATH");
		return false;
	}
	if (!plasma_session()) {
		matuwall_log_info(
			"backend", "plasma skipped: not a Plasma session");
		return false;
	}
	return true;
}

static bool plasma_apply(
	const char *path, const struct matuwall_apply_opts *opts) {
	// the client pastes the path into a script and refuses any quote
	if (strchr(path, '\'') != NULL) {
		matuwall_log_error("backend",
			"Plasma cannot set a wallpaper whose path contains '");
		return false;
	}
	const char *const head[] = {PLASMA_CLIENT, NULL};
	const char *const tail[] = {path, NULL};
	return matuwall_backend_run(PLASMA_CLIENT, head, opts, tail);
}

const struct matuwall_backend matuwall_backend_plasma = {
	.name = "plasma",
	.client = PLASMA_CLIENT,
	.detect = plasma_detect,
	.apply = plasma_apply,
};
