#ifndef MATUWALL_BACKEND_BACKEND_H
#define MATUWALL_BACKEND_BACKEND_H

#include <stdbool.h>

struct matuwall_backend {
	const char *name;
	bool (*detect)(void);		 // is this daemon available
	bool (*apply)(const char *path); // set the wallpaper, wait, report
};

const struct matuwall_backend *matuwall_backend_select(const char *name);
bool matuwall_backend_name_valid(const char *name);

bool matuwall_backend_available(const char *file);

// Is $XDG_RUNTIME_DIR/<leaf> a live socket? Proves the daemon is up, not just
// installed
bool matuwall_backend_socket_ready(const char *leaf);

bool matuwall_backend_run(const char *file, char *const argv[]);

extern const struct matuwall_backend matuwall_backend_sweetbg;
extern const struct matuwall_backend matuwall_backend_awww;

#endif
