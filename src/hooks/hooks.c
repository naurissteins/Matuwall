#include "hooks/hooks.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "util/log.h"

#define HOOK_MAX_ARGS 64
#define HOOK_CMD_MAX 4096

// Split a command template on whitespace into argv tokens, in place. Returns
// the token count, or 0 if empty or more than max tokens
static size_t tokenize(char *cmd, char *out[], size_t max) {
	size_t n = 0;
	char *p = cmd;
	for (;;) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '\0') {
			return n;
		}
		if (n >= max) {
			return 0;
		}
		out[n++] = p;
		while (*p != '\0' && *p != ' ' && *p != '\t') {
			p++;
		}
		if (*p != '\0') {
			*p++ = '\0';
		}
	}
}

// Copy token into dst, replacing every {path} with path. The path lands inside
// a single argv element, so a filename with spaces never splits into args
static bool expand_token(const char *token, const char *path, char *dst,
	size_t avail, size_t *written) {
	size_t plen = strlen(path);
	size_t w = 0;
	for (const char *p = token; *p != '\0';) {
		if (strncmp(p, "{path}", 6) == 0) {
			if (w + plen + 1 > avail) {
				return false;
			}
			memcpy(dst + w, path, plen);
			w += plen;
			p += 6;
		} else {
			if (w + 1 + 1 > avail) {
				return false;
			}
			dst[w++] = *p++;
		}
	}
	dst[w] = '\0';
	*written = w;
	return true;
}

static void spawn_detached(char *const argv[]) {
	int exec_error[2];
	if (pipe2(exec_error, O_CLOEXEC) != 0) {
		matuwall_log_warn("hook", "cannot create exec pipe for %s: %s",
			argv[0], strerror(errno));
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(exec_error[0]);
		close(exec_error[1]);
		matuwall_log_warn(
			"hook", "cannot fork %s: %s", argv[0], strerror(errno));
		return;
	}
	if (pid == 0) {
		close(exec_error[0]);
		if (setsid() < 0) {
			int saved = errno;
			ssize_t written =
				write(exec_error[1], &saved, sizeof(saved));
			(void)written;
			_exit(127);
		}
		execvp(argv[0], argv);
		int saved = errno;
		ssize_t written = write(exec_error[1], &saved, sizeof(saved));
		(void)written;
		_exit(127);
	}
	close(exec_error[1]);
	int saved;
	ssize_t count;
	do {
		count = read(exec_error[0], &saved, sizeof(saved));
	} while (count < 0 && errno == EINTR);
	int read_error = errno;
	close(exec_error[0]);
	if (count == (ssize_t)sizeof(saved)) {
		matuwall_log_warn("hook", "cannot start %s: %s", argv[0],
			strerror(saved));
	} else if (count < 0) {
		matuwall_log_warn("hook", "cannot inspect %s startup: %s",
			argv[0], strerror(read_error));
	} else {
		matuwall_log_info("hook", "started %s", argv[0]);
	}
}

static void run_one(const char *command, const char *path) {
	char tmpl[MATUWALL_HOOK_MAX];
	size_t length = strnlen(command, sizeof(tmpl));
	if (length == sizeof(tmpl)) {
		matuwall_log_warn(
			"hook", "command template is too long; skipped");
		return;
	}
	memcpy(tmpl, command, length + 1);

	char *raw[HOOK_MAX_ARGS];
	size_t n = tokenize(tmpl, raw, HOOK_MAX_ARGS);
	if (n == 0) {
		return;
	}

	char buf[HOOK_CMD_MAX];
	char *argv[HOOK_MAX_ARGS + 1];
	size_t used = 0;
	for (size_t i = 0; i < n; i++) {
		size_t written;
		if (!expand_token(raw[i], path, buf + used, sizeof(buf) - used,
			    &written)) {
			matuwall_log_warn("hook",
				"%s command is too long; skipped", raw[0]);
			return;
		}
		argv[i] = buf + used;
		used += written + 1;
	}
	argv[n] = NULL;
	spawn_detached(argv);
}

void matuwall_hooks_run(const struct matuwall_config *cfg, const char *path) {
	for (size_t i = 0; i < cfg->on_apply_count; i++) {
		run_one(cfg->on_apply[i], path);
	}
}
