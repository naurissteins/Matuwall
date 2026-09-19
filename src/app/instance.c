#include "app/instance.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "util/clock.h"
#include "util/log.h"

#define INSTANCE_WAIT_MS 5000u
#define INSTANCE_RETRY_MS 10
#define INSTANCE_REPLACE_MS 250

static uint64_t display_hash(void) {
	const char *display = getenv("WAYLAND_DISPLAY");
	if (display == NULL || display[0] == '\0') {
		display = "wayland-0";
	}

	uint64_t hash = UINT64_C(14695981039346656037);
	for (const unsigned char *p = (const unsigned char *)display;
		*p != '\0'; p++) {
		hash ^= *p;
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static bool runtime_dir(char *path, size_t path_size) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || runtime[0] != '/') {
		matuwall_log_error("instance",
			"XDG_RUNTIME_DIR must name an absolute directory");
		return false;
	}
	int length = snprintf(path, path_size, "%s/matuwall", runtime);
	if (length <= 0 || (size_t)length >= path_size) {
		matuwall_log_error(
			"instance", "runtime directory path is too long");
		return false;
	}
	return true;
}

static int open_runtime_dir(void) {
	char path[PATH_MAX];
	if (!runtime_dir(path, sizeof(path))) {
		return -1;
	}
	if (mkdir(path, 0700) != 0 && errno != EEXIST) {
		matuwall_log_error("instance", "cannot create %s: %s", path,
			strerror(errno));
		return -1;
	}

	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		matuwall_log_error("instance", "cannot open %s: %s", path,
			strerror(errno));
		return -1;
	}
	struct stat info;
	if (fstat(fd, &info) != 0 || !S_ISDIR(info.st_mode) ||
		info.st_uid != getuid() || fchmod(fd, 0700) != 0) {
		matuwall_log_error(
			"instance", "%s is not a private user directory", path);
		close(fd);
		return -1;
	}
	return fd;
}

static bool make_leaf(
	char *leaf, size_t leaf_size, const char *kind, uint64_t hash) {
	int length = snprintf(
		leaf, leaf_size, "picker-%016" PRIx64 ".%s", hash, kind);
	return length > 0 && (size_t)length < leaf_size;
}

static int open_lock(int dir_fd, uint64_t hash) {
	char leaf[64];
	if (!make_leaf(leaf, sizeof(leaf), "lock", hash)) {
		return -1;
	}
	int fd = openat(dir_fd, leaf,
		O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
	if (fd < 0) {
		return -1;
	}
	struct stat info;
	if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
		info.st_uid != getuid() || fchmod(fd, 0600) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

// The kernel drops this lease on every exit path, including crashes
static int try_lock(int fd) {
	struct flock lock = {
		.l_type = F_WRLCK,
		.l_whence = SEEK_SET,
	};
	if (fcntl(fd, F_SETLK, &lock) == 0) {
		return 1;
	}
	if (errno == EACCES || errno == EAGAIN) {
		return 0;
	}
	return -1;
}

static int make_socket(void) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}
	int descriptor_flags = fcntl(fd, F_GETFD);
	int status_flags = fcntl(fd, F_GETFL);
	if (descriptor_flags < 0 || status_flags < 0 ||
		fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0 ||
		fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool socket_address(
	const char *path, struct sockaddr_un *address, socklen_t *size) {
	size_t length = strlen(path);
	if (length >= sizeof(address->sun_path)) {
		return false;
	}
	*address = (struct sockaddr_un){
		.sun_family = AF_UNIX,
	};
	memcpy(address->sun_path, path, length + 1);
	*size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length +
			    1);
	return true;
}

static bool request_replace(const char *path) {
	struct sockaddr_un address;
	socklen_t size;
	if (!socket_address(path, &address, &size)) {
		return false;
	}
	int fd = make_socket();
	if (fd < 0) {
		return false;
	}
	bool connected =
		connect(fd, (const struct sockaddr *)&address, size) == 0;
	if (close(fd) != 0) {
		connected = false;
	}
	return connected;
}

static bool remove_stale_socket(const char *path) {
	struct stat info;
	if (lstat(path, &info) != 0) {
		return errno == ENOENT;
	}
	if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid()) {
		matuwall_log_error("instance",
			"refusing to replace unsafe runtime path %s", path);
		return false;
	}
	if (unlink(path) != 0) {
		matuwall_log_error("instance", "cannot remove stale socket: %s",
			strerror(errno));
		return false;
	}
	return true;
}

static bool listen_for_replacement(struct matuwall_instance *instance) {
	if (!remove_stale_socket(instance->socket_path)) {
		return false;
	}
	struct sockaddr_un address;
	socklen_t size;
	if (!socket_address(instance->socket_path, &address, &size)) {
		matuwall_log_error(
			"instance", "runtime socket path is too long");
		return false;
	}

	instance->socket_fd = make_socket();
	if (instance->socket_fd < 0 ||
		bind(instance->socket_fd, (const struct sockaddr *)&address,
			size) != 0) {
		matuwall_log_error("instance", "cannot bind runtime socket: %s",
			strerror(errno));
		return false;
	}
	instance->owns_socket_path = true;
	if (chmod(instance->socket_path, 0600) != 0 ||
		listen(instance->socket_fd, 8) != 0) {
		matuwall_log_error("instance",
			"cannot listen on runtime socket: %s", strerror(errno));
		return false;
	}
	return true;
}

static bool build_socket_path(
	struct matuwall_instance *instance, uint64_t hash) {
	char socket_leaf[64];
	if (!make_leaf(socket_leaf, sizeof(socket_leaf), "sock", hash)) {
		return false;
	}
	char directory[PATH_MAX];
	if (!runtime_dir(directory, sizeof(directory))) {
		return false;
	}
	int path_length = snprintf(instance->socket_path,
		sizeof(instance->socket_path), "%s/%s", directory, socket_leaf);
	return path_length > 0 &&
	       (size_t)path_length < sizeof(instance->socket_path);
}

static bool wait_for_lease(struct matuwall_instance *instance) {
	int64_t next_request = matuwall_now_ms();
	int64_t deadline = next_request + INSTANCE_WAIT_MS;
	bool replacement_requested = false;
	for (;;) {
		int locked = try_lock(instance->lock_fd);
		if (locked < 0) {
			matuwall_log_error("instance",
				"cannot acquire instance lock: %s",
				strerror(errno));
			return false;
		}
		if (locked > 0) {
			break;
		}
		int64_t now = matuwall_now_ms();
		if (now >= deadline) {
			matuwall_log_error("instance",
				"the existing picker did not close within %u "
				"ms",
				INSTANCE_WAIT_MS);
			return false;
		}
		// Retry for a late listener or a new lease owner
		if (now >= next_request) {
			if (request_replace(instance->socket_path)) {
				replacement_requested = true;
			}
			next_request = now + INSTANCE_REPLACE_MS;
		}
		if (poll(NULL, 0, INSTANCE_RETRY_MS) < 0 && errno != EINTR) {
			matuwall_log_error("instance",
				"cannot wait for the existing picker");
			return false;
		}
	}

	if (replacement_requested) {
		matuwall_log_info("instance", "replaced the previous picker");
	}
	return true;
}

bool matuwall_instance_init(struct matuwall_instance *instance) {
	*instance = (struct matuwall_instance){
		.lock_fd = -1,
		.socket_fd = -1,
	};

	uint64_t hash = display_hash();
	int dir_fd = open_runtime_dir();
	if (dir_fd < 0) {
		return false;
	}
	if (!build_socket_path(instance, hash)) {
		matuwall_log_error(
			"instance", "runtime socket path is too long");
		close(dir_fd);
		return false;
	}
	instance->lock_fd = open_lock(dir_fd, hash);
	if (instance->lock_fd < 0) {
		matuwall_log_error(
			"instance", "cannot open the runtime instance lock");
		close(dir_fd);
		return false;
	}
	if (close(dir_fd) != 0) {
		matuwall_log_error(
			"instance", "cannot close the runtime directory");
		return false;
	}

	return wait_for_lease(instance) && listen_for_replacement(instance);
}

int matuwall_instance_fd(const struct matuwall_instance *instance) {
	return instance->socket_fd;
}

bool matuwall_instance_dispatch(
	struct matuwall_instance *instance, bool *replace_requested) {
	*replace_requested = false;
	for (;;) {
		int fd = accept(instance->socket_fd, NULL, NULL);
		if (fd < 0 && errno == EINTR) {
			continue;
		}
		if (fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return true;
		}
		if (fd < 0) {
			matuwall_log_error("instance",
				"cannot accept a replacement request");
			return false;
		}
		*replace_requested = true;
		if (close(fd) != 0) {
			matuwall_log_warn("instance",
				"cannot close a replacement connection");
		}
	}
}

void matuwall_instance_finish(struct matuwall_instance *instance) {
	if (instance->socket_fd >= 0) {
		if (close(instance->socket_fd) != 0) {
			matuwall_log_warn(
				"instance", "cannot close runtime socket");
		}
		instance->socket_fd = -1;
	}
	if (instance->owns_socket_path) {
		if (unlink(instance->socket_path) != 0 && errno != ENOENT) {
			matuwall_log_warn(
				"instance", "cannot remove runtime socket");
		}
		instance->owns_socket_path = false;
	}
	if (instance->lock_fd >= 0) {
		if (close(instance->lock_fd) != 0) {
			matuwall_log_warn(
				"instance", "cannot release instance lock");
		}
		instance->lock_fd = -1;
	}
}
