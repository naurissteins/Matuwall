#include <stddef.h>

#include "backend/backend.h"
#include "util/log.h"

// sweetbg is the user's wallpaper daemon, driven by `sweetbg img <path>`

// The socket check keeps "auto" from picking an installed-but-dead sweetbg
// and stopping before it reaches the next backend
static bool sweetbg_detect(void) {
	if (!matuwall_backend_available("sweetbg")) {
		matuwall_log_info(
			"backend", "sweetbg skipped: client not found on PATH");
		return false;
	}
	if (!matuwall_backend_socket_ready("sweetbg.sock")) {
		matuwall_log_info(
			"backend", "sweetbg skipped: daemon socket is missing");
		return false;
	}
	return true;
}

static bool sweetbg_apply(const char *path) {
	// sweetbg resolves the path to absolute itself
	char *const argv[] = {"sweetbg", "img", (char *)path, NULL};
	return matuwall_backend_run("sweetbg", argv);
}

const struct matuwall_backend matuwall_backend_sweetbg = {
	.name = "sweetbg",
	.detect = sweetbg_detect,
	.apply = sweetbg_apply,
};
