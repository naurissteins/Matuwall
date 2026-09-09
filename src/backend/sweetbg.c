#include <stddef.h>

#include "backend/backend.h"
#include "util/log.h"

// sweetbg is the user's wallpaper daemon, driven by `sweetbg img <path>`

// The socket check keeps "auto" from picking an installed-but-dead sweetbg
// and stopping before it reaches the next backend
static bool sweetbg_detect(void) {
	if (!sweetwall_backend_available("sweetbg")) {
		sweetwall_log_info(
			"backend", "sweetbg skipped: client not found on PATH");
		return false;
	}
	if (!sweetwall_backend_socket_ready("sweetbg.sock")) {
		sweetwall_log_info(
			"backend", "sweetbg skipped: daemon socket is missing");
		return false;
	}
	return true;
}

static bool sweetbg_apply(const char *path) {
	// sweetbg resolves the path to absolute itself
	char *const argv[] = {"sweetbg", "img", (char *)path, NULL};
	return sweetwall_backend_run("sweetbg", argv);
}

const struct sweetwall_backend sweetwall_backend_sweetbg = {
	.name = "sweetbg",
	.detect = sweetbg_detect,
	.apply = sweetbg_apply,
};
