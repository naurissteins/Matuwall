#include "state/selection.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/fs.h"

#define STATE_MAGIC 0x5357534cu // "SWSL"
#define STATE_VERSION 1u
#define STATE_FILE "last-selection"

struct selection_header {
	uint32_t magic;
	uint32_t version;
	uint32_t path_length;
};

bool matuwall_selection_path(char *path, size_t path_size) {
	char dir[PATH_MAX];
	if (!matuwall_fs_state_dir(dir, sizeof(dir))) {
		return false;
	}
	int length = snprintf(path, path_size, "%s/%s", dir, STATE_FILE);
	return length > 0 && (size_t)length < path_size;
}

static int open_state_dir(bool create) {
	char path[PATH_MAX];
	if (!matuwall_fs_state_dir(path, sizeof(path))) {
		return -1;
	}
	if (create && !matuwall_fs_make_dirs(path, 0700)) {
		return -1;
	}
	return open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

bool matuwall_selection_load(char *path, size_t path_size) {
	if (path_size == 0) {
		return false;
	}
	path[0] = '\0';

	int dir_fd = open_state_dir(false);
	if (dir_fd < 0) {
		return false;
	}
	int fd = openat(dir_fd, STATE_FILE,
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		close(dir_fd);
		return false;
	}

	struct selection_header header;
	struct stat info;
	bool ok =
		fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
		matuwall_fs_read_all(fd, &header, sizeof(header)) &&
		header.magic == STATE_MAGIC &&
		header.version == STATE_VERSION && header.path_length > 0 &&
		header.path_length < path_size && info.st_size >= 0 &&
		(uint64_t)info.st_size == sizeof(header) + header.path_length &&
		matuwall_fs_read_all(fd, path, header.path_length) &&
		memchr(path, '\0', header.path_length) == NULL;
	if (ok) {
		path[header.path_length] = '\0';
	} else {
		path[0] = '\0';
	}
	if (close(fd) != 0) {
		ok = false;
	}
	if (close(dir_fd) != 0) {
		ok = false;
	}
	return ok;
}

static int open_temporary(int dir_fd, char *name, size_t name_size) {
	for (unsigned int attempt = 0; attempt < 10; attempt++) {
		int length = snprintf(name, name_size, ".selection.%ld.%u.tmp",
			(long)getpid(), attempt);
		if (length < 0 || (size_t)length >= name_size) {
			return -1;
		}
		int fd = openat(dir_fd, name,
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
			0600);
		if (fd >= 0 || errno != EEXIST) {
			return fd;
		}
	}
	return -1;
}

bool matuwall_selection_save(const char *path) {
	size_t path_length = strlen(path);
	if (path_length == 0 || path_length > UINT32_MAX) {
		return false;
	}

	int dir_fd = open_state_dir(true);
	if (dir_fd < 0) {
		return false;
	}
	char temporary[64];
	int fd = open_temporary(dir_fd, temporary, sizeof(temporary));
	if (fd < 0) {
		close(dir_fd);
		return false;
	}

	struct selection_header header = {
		.magic = STATE_MAGIC,
		.version = STATE_VERSION,
		.path_length = (uint32_t)path_length,
	};
	bool ok = matuwall_fs_write_all(fd, &header, sizeof(header)) &&
		  matuwall_fs_write_all(fd, path, path_length);
	if (close(fd) != 0) {
		ok = false;
	}
	if (ok && renameat(dir_fd, temporary, dir_fd, STATE_FILE) != 0) {
		ok = false;
	}
	if (!ok && unlinkat(dir_fd, temporary, 0) != 0 && errno != ENOENT) {
		ok = false;
	}
	if (close(dir_fd) != 0) {
		ok = false;
	}
	return ok;
}
