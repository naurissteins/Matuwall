#include "state/favorites.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "scan/dirscan.h"
#include "util/fs.h"
#include "util/log.h"

// bare name inside directory, unless the list would read it as a comment or ~
static const char *entry_name(const char *path, const char *directory) {
	size_t length = strlen(directory);
	if (length == 0 || strncmp(path, directory, length) != 0 ||
		path[length] != '/') {
		return path;
	}
	const char *name = path + length + 1;
	return name[0] == '#' || name[0] == '~' ? path : name;
}

static bool listed(const struct matuwall_dirscan *list, const char *path) {
	for (size_t i = 0; i < list->count; i++) {
		if (strcmp(list->paths[i], path) == 0) {
			return true;
		}
	}
	return false;
}

static bool make_parent(const char *file) {
	char dir[PATH_MAX];
	const char *slash = strrchr(file, '/');
	size_t length = slash == NULL ? 0 : (size_t)(slash - file);
	if (length == 0) {
		return true;
	}
	if (length >= sizeof(dir)) {
		return false;
	}
	memcpy(dir, file, length);
	dir[length] = '\0';
	return matuwall_fs_make_dirs(dir, 0700);
}

// the last line may lack its newline after a hand edit
static bool ends_open(FILE *fp, bool *newline) {
	*newline = fseek(fp, -1, SEEK_END) == 0 && fgetc(fp) != '\n';
	return fseek(fp, 0, SEEK_END) == 0;
}

static size_t append(FILE *fp, bool newline,
	const struct matuwall_dirscan *existing, const char *directory,
	char *const *paths, const bool *marked, size_t count) {
	size_t added = 0;
	for (size_t i = 0; i < count; i++) {
		// a line break in a name would split it into two entries
		if (!marked[i] || listed(existing, paths[i]) ||
			strpbrk(paths[i], "\r\n") != NULL) {
			continue;
		}
		fprintf(fp, "%s%s\n", newline ? "\n" : "",
			entry_name(paths[i], directory));
		newline = false;
		added++;
	}
	return added;
}

bool matuwall_favorites_add(const char *file, const char *directory,
	char *const *paths, const bool *marked, size_t count) {
	if (file[0] == '\0') {
		matuwall_log_warn(
			"favorites", "file is unresolved (is HOME set?)");
		return false;
	}
	// a missing file is an empty list, its read already logs other errors
	struct matuwall_dirscan existing = {0};
	if (access(file, F_OK) == 0 &&
		(!matuwall_dirscan_list(&existing, file, directory) ||
			existing.unavailable)) {
		matuwall_dirscan_finish(&existing);
		return false;
	}

	FILE *fp = make_parent(file) ? fopen(file, "a+e") : NULL;
	if (fp == NULL) {
		matuwall_log_warn("favorites", "cannot open %s: %s", file,
			strerror(errno));
		matuwall_dirscan_finish(&existing);
		return false;
	}
	bool newline;
	bool ok = ends_open(fp, &newline);
	size_t added = ok ? append(fp, newline, &existing, directory, paths,
				    marked, count)
			  : 0;
	matuwall_dirscan_finish(&existing);
	ok = ok && !ferror(fp);
	ok = fclose(fp) == 0 && ok;
	if (!ok) {
		matuwall_log_warn("favorites", "cannot write %s", file);
		return false;
	}
	matuwall_log_info("favorites", "added %zu to %s", added, file);
	return true;
}
