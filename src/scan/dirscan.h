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

// recursive walks subdirectories breadth-first, bounded in depth and entries
bool matuwall_dirscan_run(
	struct matuwall_dirscan *scan, const char *dir, bool recursive);

// one path per line in file order, "-" reads stdin, blank and # lines skip
// relative entries resolve against directory, or the cwd for stdin
bool matuwall_dirscan_list(
	struct matuwall_dirscan *scan, const char *list, const char *directory);

void matuwall_dirscan_finish(struct matuwall_dirscan *scan);

// canonical absolute path of one scanned entry, resolved holds PATH_MAX bytes
bool matuwall_dirscan_resolve(const char *path, char *resolved);

#endif
