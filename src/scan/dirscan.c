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
// bound the scan by entries read, not image count, so startup stays flat
#define MAX_DEPTH 8
#define MAX_ENTRIES 8192
#define MAX_DIRS 256

// breadth-first queue of subdirectories, so top-level images always come first
struct walk {
	char **dirs;
	size_t head;
	size_t count;
	size_t capacity;
	size_t entries;
	size_t unreadable;
	bool dirs_capped;
};

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

// d_type is free, links and DT_UNKNOWN are checked when opened
static bool may_be_image(unsigned char type) {
	return type == DT_REG || type == DT_LNK || type == DT_UNKNOWN;
}

static bool push_item(
	char ***items, size_t *count, size_t *capacity, char *item) {
	if (*count == *capacity) {
		size_t grown =
			*capacity == 0 ? INITIAL_CAPACITY : *capacity * 2;
		char **resized = (char **)realloc(
			(void *)*items, grown * sizeof(**items));
		if (resized == NULL) {
			return false;
		}
		*items = resized;
		*capacity = grown;
	}
	(*items)[(*count)++] = item;
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

// takes ownership of path; false only on allocation failure
static bool push_owned(
	char ***items, size_t *count, size_t *capacity, char *path) {
	if (path != NULL && push_item(items, count, capacity, path)) {
		return true;
	}
	free(path);
	return false;
}

// reads one directory and closes it; false only on allocation failure
static bool scan_dir(struct matuwall_dirscan *scan, struct walk *walk,
	DIR *handle, const char *dir, bool descend) {
	bool ok = true;
	struct dirent *entry;
	while (ok && (entry = readdir(handle)) != NULL) {
		// hidden entries cost a readdir too
		if (++walk->entries > MAX_ENTRIES) {
			scan->truncated = true;
			break;
		}
		if (entry->d_name[0] == '.') {
			continue;
		}
		// no stat: symlinked and DT_UNKNOWN directories are not entered
		if (descend && entry->d_type == DT_DIR) {
			if (walk->count == MAX_DIRS) {
				walk->dirs_capped = true;
				continue;
			}
			ok = push_owned(&walk->dirs, &walk->count,
				&walk->capacity, join_path(dir, entry->d_name));
			continue;
		}
		if (!may_be_image(entry->d_type) ||
			!has_supported_extension(entry->d_name)) {
			continue;
		}
		if (scan->count == MAX_ITEMS) {
			scan->truncated = true;
			break;
		}
		ok = push_owned(&scan->paths, &scan->count, &scan->capacity,
			join_path(dir, entry->d_name));
	}
	closedir(handle);
	return ok;
}

static bool walk_subdirs(struct matuwall_dirscan *scan, struct walk *walk) {
	bool ok = true;
	size_t level_end = walk->count;
	size_t depth = 1;
	while (ok && !scan->truncated && walk->head < walk->count) {
		if (walk->head == level_end) {
			depth++;
			level_end = walk->count;
		}
		char *dir = walk->dirs[walk->head];
		walk->dirs[walk->head++] = NULL;
		DIR *handle = opendir(dir);
		if (handle == NULL) {
			walk->unreadable++;
		} else {
			ok = scan_dir(
				scan, walk, handle, dir, depth < MAX_DEPTH);
		}
		free(dir);
	}
	return ok;
}

static void walk_finish(struct walk *walk) {
	for (size_t i = walk->head; i < walk->count; i++) {
		free(walk->dirs[i]);
	}
	free((void *)walk->dirs);
}

bool matuwall_dirscan_run(
	struct matuwall_dirscan *scan, const char *dir, bool recursive) {
	*scan = (struct matuwall_dirscan){0};

	DIR *handle = opendir(dir);
	if (handle == NULL) {
		scan->unavailable = true;
		matuwall_log_error("wallpapers",
			"directory unavailable: %s: %s", dir, strerror(errno));
		return true;
	}

	struct walk walk = {0};
	// top level first, so it reads the same with or without recursion
	bool ok = scan_dir(scan, &walk, handle, dir, recursive);
	ok = ok && walk_subdirs(scan, &walk);
	walk_finish(&walk);
	scan->truncated = scan->truncated || walk.dirs_capped;
	if (!ok) {
		matuwall_dirscan_finish(scan);
		return false;
	}

	if (walk.unreadable > 0) {
		matuwall_log_warn("wallpapers",
			"skipped %zu unreadable subdirector%s under %s",
			walk.unreadable, walk.unreadable == 1 ? "y" : "ies",
			dir);
	}
	if (scan->truncated) {
		matuwall_log_warn("wallpapers",
			"%s: scan limit reached; showing %zu images", dir,
			scan->count);
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
