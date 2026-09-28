#ifndef MATUWALL_BACKEND_BACKEND_H
#define MATUWALL_BACKEND_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

struct matuwall_apply_opts {
	// user flags for the daemon's img command, placed before the path
	const char *const *args;
	size_t arg_count;
	// [backend.command] apply template, the command row's whole argv
	const char *command;
};

struct matuwall_backend {
	const char *name;
	// program run on apply, NULL when it comes from config
	const char *client;
	bool (*detect)(void); // is this daemon available
	// set the wallpaper, wait, report
	bool (*apply)(const char *path, const struct matuwall_apply_opts *opts);
};

const struct matuwall_backend *matuwall_backend_select(const char *name);
bool matuwall_backend_name_valid(const char *name);

bool matuwall_backend_available(const char *file);

// Is $XDG_RUNTIME_DIR/<leaf> a live socket? Proves the daemon is up, not just
// installed
bool matuwall_backend_socket_ready(const char *leaf);

// run head + opts->args + tail as one argv, both lists are NULL-terminated
bool matuwall_backend_run(const char *file, const char *const head[],
	const struct matuwall_apply_opts *opts, const char *const tail[]);

extern const struct matuwall_backend matuwall_backend_sweetbg;
extern const struct matuwall_backend matuwall_backend_awww;
extern const struct matuwall_backend matuwall_backend_command;
extern const struct matuwall_backend matuwall_backend_plasma;

#endif
