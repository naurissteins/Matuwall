#include "hooks/hooks.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "sweetwall: cannot fork hook %s: %s\n", argv[0],
			strerror(errno));
		return;
	}
	if (pid == 0) {
		setsid();
		execvp(argv[0], argv);
		fprintf(stderr, "sweetwall: cannot run hook %s: %s\n", argv[0],
			strerror(errno));
		_exit(127);
	}
}

static void run_one(const char *command, const char *path) {
	char tmpl[SWEETWALL_HOOK_MAX];
	memcpy(tmpl, command, strlen(command) + 1);

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
			fprintf(stderr,
				"sweetwall: hook too long, skipping: %s\n",
				command);
			return;
		}
		argv[i] = buf + used;
		used += written + 1;
	}
	argv[n] = NULL;
	spawn_detached(argv);
}

void sweetwall_hooks_run(const struct sweetwall_config *cfg, const char *path) {
	for (size_t i = 0; i < cfg->on_apply_count; i++) {
		run_one(cfg->on_apply[i], path);
	}
}
