#include "backend/backend.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/log.h"

// Probe order for backend = "auto"
static const struct sweetwall_backend *const backends[] = {
	&sweetwall_backend_sweetbg,
	&sweetwall_backend_awww,
};

const struct sweetwall_backend *sweetwall_backend_select(const char *name) {
	size_t count = sizeof(backends) / sizeof(backends[0]);

	if (name != NULL && name[0] != '\0' && strcmp(name, "auto") != 0) {
		for (size_t i = 0; i < count; i++) {
			if (strcmp(backends[i]->name, name) == 0) {
				return backends[i];
			}
		}
		return NULL;
	}

	// auto: first backend that detects itself
	for (size_t i = 0; i < count; i++) {
		if (backends[i]->detect()) {
			sweetwall_log_info("backend", "auto selected %s",
				backends[i]->name);
			return backends[i];
		}
	}
	return NULL;
}

bool sweetwall_backend_available(const char *file) {
	const char *path = getenv("PATH");
	if (path == NULL || file == NULL) {
		return false;
	}

	char candidate[PATH_MAX];
	while (*path != '\0') {
		const char *sep = strchr(path, ':');
		size_t len = sep != NULL ? (size_t)(sep - path) : strlen(path);

		if (len > 0 && len < sizeof(candidate)) {
			int n = snprintf(candidate, sizeof(candidate),
				"%.*s/%s", (int)len, path, file);
			if (n > 0 && (size_t)n < sizeof(candidate) &&
				access(candidate, X_OK) == 0) {
				return true;
			}
		}

		if (sep == NULL) {
			break;
		}
		path = sep + 1;
	}
	return false;
}

bool sweetwall_backend_socket_ready(const char *leaf) {
	if (leaf == NULL || leaf[0] == '\0') {
		return false;
	}

	const char *dir = getenv("XDG_RUNTIME_DIR");
	char fallback[64];
	if (dir == NULL || dir[0] != '/') {
		int n = snprintf(fallback, sizeof(fallback), "/run/user/%lu",
			(unsigned long)getuid());
		if (n < 0 || (size_t)n >= sizeof(fallback)) {
			return false;
		}
		dir = fallback;
	}

	char path[PATH_MAX];
	int n = snprintf(path, sizeof(path), "%s/%s", dir, leaf);
	if (n < 0 || (size_t)n >= sizeof(path)) {
		return false;
	}

	struct stat st;
	return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
}

bool sweetwall_backend_run(const char *file, char *const argv[]) {
	int exec_error[2];
	if (pipe2(exec_error, O_CLOEXEC) != 0) {
		sweetwall_log_error("backend", "cannot create exec pipe: %s",
			strerror(errno));
		return false;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(exec_error[0]);
		close(exec_error[1]);
		sweetwall_log_error(
			"backend", "cannot fork %s: %s", file, strerror(errno));
		return false;
	}
	if (pid == 0) {
		close(exec_error[0]);
		execvp(file, argv);
		int saved = errno;
		ssize_t written = write(exec_error[1], &saved, sizeof(saved));
		(void)written;
		_exit(127);
	}
	close(exec_error[1]);

	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			close(exec_error[0]);
			sweetwall_log_error("backend", "wait for %s failed: %s",
				file, strerror(errno));
			return false;
		}
	}

	int saved;
	ssize_t count;
	do {
		count = read(exec_error[0], &saved, sizeof(saved));
	} while (count < 0 && errno == EINTR);
	int read_error = errno;
	close(exec_error[0]);
	if (count < 0) {
		sweetwall_log_error("backend", "cannot inspect %s startup: %s",
			file, strerror(read_error));
		return false;
	}
	if (count == (ssize_t)sizeof(saved)) {
		sweetwall_log_error(
			"backend", "cannot run %s: %s", file, strerror(saved));
		return false;
	}
	if (WIFSIGNALED(status)) {
		sweetwall_log_error("backend", "%s terminated by signal %d",
			file, WTERMSIG(status));
		return false;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		sweetwall_log_error("backend", "%s exited with status %d", file,
			WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return false;
	}
	return true;
}
