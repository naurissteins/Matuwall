#include <stddef.h>

#include "backend/backend.h"

// sweetbg is the user's wallpaper daemon, driven by `sweetbg img <path>`

static bool sweetbg_detect(void) {
	return sweetwall_backend_available("sweetbg");
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
