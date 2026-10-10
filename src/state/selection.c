#include "state/selection.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/fs.h"

#define STATE_FILE "last-selection"

bool matuwall_selection_path(char *path, size_t path_size) {
	char dir[PATH_MAX];
	if (!matuwall_fs_state_dir(dir, sizeof(dir))) {
		return false;
	}
	int length = snprintf(path, path_size, "%s/%s", dir, STATE_FILE);
	return length > 0 && (size_t)length < path_size;
}

// file holds the raw path bytes, nothing else
bool matuwall_selection_load(char *path, size_t path_size) {
	if (path_size == 0) {
		return false;
	}
	char file[PATH_MAX];
	if (!matuwall_selection_path(file, sizeof(file))) {
		path[0] = '\0';
		return false;
	}
	int fd = open(file, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		path[0] = '\0';
		return false;
	}
	struct stat info;
	ssize_t length = -1;
	if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode)) {
		length = read(fd, path, path_size);
	}
	bool ok = close(fd) == 0 && length > 0 && (size_t)length < path_size &&
		  path[0] == '/' && memchr(path, '\0', (size_t)length) == NULL;
	path[ok ? (size_t)length : 0] = '\0';
	return ok;
}

bool matuwall_selection_save(const char *path) {
	char dir[PATH_MAX];
	char file[PATH_MAX];
	char tmp[PATH_MAX];
	if (!matuwall_fs_state_dir(dir, sizeof(dir)) ||
		!matuwall_fs_make_dirs(dir, 0700) ||
		!matuwall_selection_path(file, sizeof(file)) ||
		(size_t)snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", file) >=
			sizeof(tmp)) {
		return false;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		return false;
	}
	bool ok = matuwall_fs_write_all(fd, path, strlen(path));
	// publish only a complete file
	if (close(fd) != 0 || !ok || rename(tmp, file) != 0) {
		unlink(tmp);
		return false;
	}
	return true;
}
