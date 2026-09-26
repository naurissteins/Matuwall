#include "util/fs.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool matuwall_fs_state_dir(char *out, size_t out_size) {
	const char *xdg = getenv("XDG_STATE_HOME");
	if (xdg != NULL && xdg[0] == '/') {
		return (size_t)snprintf(out, out_size, "%s/matuwall", xdg) <
		       out_size;
	}
	const char *home = getenv("HOME");
	if (home == NULL) {
		return false;
	}
	return (size_t)snprintf(out, out_size, "%s/.local/state/matuwall",
		       home) < out_size;
}

bool matuwall_fs_make_dirs(const char *path, mode_t mode) {
	char copy[PATH_MAX];
	size_t length = strlen(path);
	if (length == 0 || length >= sizeof(copy)) {
		return false;
	}
	memcpy(copy, path, length + 1);

	for (char *p = copy + 1; *p != '\0'; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(copy, mode) != 0 && errno != EEXIST) {
			return false;
		}
		*p = '/';
	}
	return mkdir(copy, mode) == 0 || errno == EEXIST;
}

bool matuwall_fs_read_all(int fd, void *data, size_t size) {
	uint8_t *bytes = data;
	while (size > 0) {
		ssize_t count = read(fd, bytes, size);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return false;
		}
		bytes += (size_t)count;
		size -= (size_t)count;
	}
	return true;
}

bool matuwall_fs_write_all(int fd, const void *data, size_t size) {
	const uint8_t *bytes = data;
	while (size > 0) {
		ssize_t count = write(fd, bytes, size);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return false;
		}
		bytes += (size_t)count;
		size -= (size_t)count;
	}
	return true;
}
