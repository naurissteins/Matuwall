#ifndef SWEETWALL_BACKEND_BACKEND_H
#define SWEETWALL_BACKEND_BACKEND_H

#include <stdbool.h>

struct sweetwall_backend {
	const char *name;
	bool (*detect)(void);		 // is this daemon available
	bool (*apply)(const char *path); // set the wallpaper, wait, report
};

const struct sweetwall_backend *sweetwall_backend_select(const char *name);

bool sweetwall_backend_available(const char *file);

bool sweetwall_backend_run(const char *file, char *const argv[]);

extern const struct sweetwall_backend sweetwall_backend_sweetbg;

#endif
