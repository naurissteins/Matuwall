#include "hooks/hooks.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "util/command.h"
#include "util/log.h"

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
	// Config and CLI bound templates, this guards a slot with no terminator
	if (strnlen(command, MATUWALL_HOOK_MAX) == MATUWALL_HOOK_MAX) {
		matuwall_log_warn(
			"hook", "command template is too long; skipped");
		return;
	}
	struct matuwall_command cmd;
	if (!matuwall_command_expand(&cmd, command, path)) {
		matuwall_log_warn("hook", "command is too long; skipped");
		return;
	}
	if (cmd.argc > 0) {
		spawn_detached(cmd.argv);
	}
}

void matuwall_hooks_run(const struct matuwall_config *cfg, const char *path) {
	for (size_t i = 0; i < cfg->on_apply_count; i++) {
		run_one(cfg->on_apply[i], path);
	}
}
