#include <stddef.h>

#include "backend/backend.h"
#include "util/command.h"
#include "util/log.h"

// nothing proves an arbitrary command's daemon is up, so auto never picks it
static bool command_detect(void) {
	return false;
}

static bool command_apply(
	const char *path, const struct matuwall_apply_opts *opts) {
	if (opts == NULL || opts->command == NULL || opts->command[0] == '\0') {
		matuwall_log_error("backend",
			"command backend needs [backend.command] apply");
		return false;
	}
	struct matuwall_command cmd;
	if (!matuwall_command_expand(&cmd, opts->command, path)) {
		matuwall_log_error("backend", "apply command is too long");
		return false;
	}
	if (cmd.argc == 0) {
		matuwall_log_error("backend", "apply command is empty");
		return false;
	}
	const char *const none[] = {NULL};
	// execvp takes char *const[], it never writes through these
	return matuwall_backend_run(
		cmd.argv[0], (const char *const *)cmd.argv, NULL, none);
}

const struct matuwall_backend matuwall_backend_command = {
	.name = "command",
	.client = NULL,
	.detect = command_detect,
	.apply = command_apply,
};
