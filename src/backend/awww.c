#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/backend.h"
#include "util/log.h"

// awww is a swww-style daemon: the `awww` client talks to `awww-daemon` over a
// per-display socket, and `awww img <path>` sets the wallpaper

// Mirrors awww's own socket_file(): $XDG_RUNTIME_DIR/<display>-awww-daemon.sock
static bool awww_socket_ready(void) {
	const char *display = getenv("WAYLAND_DISPLAY");
	if (display == NULL || display[0] == '\0') {
		display = "wayland-0"; // awww's own fallback
	}
	// awww keys the socket on the basename when WAYLAND_DISPLAY is a path
	const char *slash = strrchr(display, '/');
	if (slash != NULL) {
		display = slash + 1;
	}
	if (display[0] == '\0') {
		return false;
	}

	char leaf[128];
	int n = snprintf(leaf, sizeof(leaf), "%s-awww-daemon.sock", display);
	if (n < 0 || (size_t)n >= sizeof(leaf)) {
		return false;
	}
	return sweetwall_backend_socket_ready(leaf);
}

// Both halves matter: the client binary applies, the socket proves the daemon
// is up. A default-namespace daemon only; -n namespaces are not probed
static bool awww_detect(void) {
	if (!sweetwall_backend_available("awww")) {
		sweetwall_log_info(
			"backend", "awww skipped: client not found on PATH");
		return false;
	}
	if (!awww_socket_ready()) {
		sweetwall_log_info("backend",
			"awww skipped: default daemon socket is missing");
		return false;
	}
	return true;
}

static bool awww_apply(const char *path) {
	// `--` keeps a path that starts with '-' from parsing as a flag; the
	// client canonicalizes the path itself before telling the daemon
	char *const argv[] = {"awww", "img", "--", (char *)path, NULL};
	return sweetwall_backend_run("awww", argv);
}

const struct sweetwall_backend sweetwall_backend_awww = {
	.name = "awww",
	.detect = awww_detect,
	.apply = awww_apply,
};
