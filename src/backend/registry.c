#include "backend/backend.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const struct sweetwall_backend *const backends[] = {
	&sweetwall_backend_sweetbg,
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

bool sweetwall_backend_run(const char *file, char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "sweetwall: fork failed: %s\n",
			strerror(errno));
		return false;
	}
	if (pid == 0) {
		execvp(file, argv);
		fprintf(stderr, "sweetwall: cannot run %s: %s\n", file,
			strerror(errno));
		_exit(127);
	}

	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			fprintf(stderr, "sweetwall: waitpid failed: %s\n",
				strerror(errno));
			return false;
		}
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
