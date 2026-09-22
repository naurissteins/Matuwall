#include "scan/dirscan.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "util/log.h"

// A picker, not a file browser; refuse to build an unbounded grid
#define MAX_ITEMS 4096
#define INITIAL_CAPACITY 64

static const char *const supported_extensions[] = {
	".jpg",
	".jpeg",
	".png",
	".webp",
};

static bool has_supported_extension(const char *name) {
	const char *dot = strrchr(name, '.');
	if (dot == NULL || dot == name) {
		return false;
	}
	size_t count =
		sizeof(supported_extensions) / sizeof(supported_extensions[0]);
	for (size_t i = 0; i < count; i++) {
		if (strcasecmp(dot, supported_extensions[i]) == 0) {
			return true;
		}
	}
	return false;
}

static bool push_path(struct matuwall_dirscan *scan, char *path) {
	if (scan->count == scan->capacity) {
		size_t capacity = scan->capacity == 0 ? INITIAL_CAPACITY
						      : scan->capacity * 2;
		char **paths = (char **)realloc(
			(void *)scan->paths, capacity * sizeof(*scan->paths));
		if (paths == NULL) {
			return false;
		}
		scan->paths = paths;
		scan->capacity = capacity;
	}
	scan->paths[scan->count++] = path;
	return true;
}

static char *join_path(const char *dir, const char *name) {
	size_t dir_len = strlen(dir);
	// Tolerate a trailing slash on the configured directory
	bool needs_slash = dir_len > 0 && dir[dir_len - 1] != '/';
	size_t size = dir_len + (needs_slash ? 1 : 0) + strlen(name) + 1;

	char *path = malloc(size);
	if (path == NULL) {
		return NULL;
	}
	snprintf(path, size, "%s%s%s", dir, needs_slash ? "/" : "", name);
	return path;
}

static int compare_paths(const void *a, const void *b) {
	const char *const *lhs = (const char *const *)a;
	const char *const *rhs = (const char *const *)b;
	return strcmp(*lhs, *rhs);
}

bool matuwall_dirscan_run(struct matuwall_dirscan *scan, const char *dir) {
	*scan = (struct matuwall_dirscan){0};

	DIR *handle = opendir(dir);
	if (handle == NULL) {
		scan->unavailable = true;
		matuwall_log_error("wallpapers",
			"directory unavailable: %s: %s", dir, strerror(errno));
		return true;
	}

	struct dirent *entry;
	while ((entry = readdir(handle)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		if (entry->d_type == DT_DIR) {
			continue;
		}
		if (!has_supported_extension(entry->d_name)) {
			continue;
		}
		if (scan->count == MAX_ITEMS) {
			scan->truncated = true;
			break;
		}

		char *path = join_path(dir, entry->d_name);
		if (path == NULL || !push_path(scan, path)) {
			free(path);
			closedir(handle);
			matuwall_dirscan_finish(scan);
			return false;
		}
	}

	closedir(handle);

	if (scan->truncated) {
		matuwall_log_warn("wallpapers",
			"%s holds more than %d images; showing the first %d",
			dir, MAX_ITEMS, MAX_ITEMS);
	}

	if (scan->count > 0) {
		qsort((void *)scan->paths, scan->count, sizeof(*scan->paths),
			compare_paths);
	}
	return true;
}

void matuwall_dirscan_finish(struct matuwall_dirscan *scan) {
	for (size_t i = 0; i < scan->count; i++) {
		free(scan->paths[i]);
	}
	free((void *)scan->paths);
	*scan = (struct matuwall_dirscan){0};
}

// scan stays stat-free, one selected entry is checked on the way out
bool matuwall_dirscan_resolve(const char *path, char *resolved) {
	if (realpath(path, resolved) == NULL) {
		matuwall_log_error("wallpapers", "cannot resolve %s: %s", path,
			strerror(errno));
		return false;
	}
	struct stat st;
	if (stat(resolved, &st) != 0) {
		matuwall_log_error("wallpapers", "cannot read %s: %s", resolved,
			strerror(errno));
		return false;
	}
	if (!S_ISREG(st.st_mode)) {
		matuwall_log_error(
			"wallpapers", "%s is not a regular file", resolved);
		return false;
	}
	return true;
}
