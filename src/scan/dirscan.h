#ifndef MATUWALL_SCAN_DIRSCAN_H
#define MATUWALL_SCAN_DIRSCAN_H

#include <stdbool.h>
#include <stddef.h>

struct matuwall_dirscan {
	char **paths;
	size_t count;
	size_t capacity;
	bool truncated;
	// A path error is renderable; false return means an internal failure
	bool unavailable;
};

bool matuwall_dirscan_run(struct matuwall_dirscan *scan, const char *dir);

void matuwall_dirscan_finish(struct matuwall_dirscan *scan);

#endif
