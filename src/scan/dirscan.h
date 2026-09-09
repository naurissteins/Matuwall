#ifndef SWEETWALL_SCAN_DIRSCAN_H
#define SWEETWALL_SCAN_DIRSCAN_H

#include <stdbool.h>
#include <stddef.h>

struct sweetwall_dirscan {
	char **paths;
	size_t count;
	size_t capacity;
	bool truncated;
};

bool sweetwall_dirscan_run(struct sweetwall_dirscan *scan, const char *dir);

void sweetwall_dirscan_finish(struct sweetwall_dirscan *scan);

#endif
