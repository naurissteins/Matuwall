#include "util/command.h"

#include <string.h>

static bool is_space(char c) {
	return c == ' ' || c == '\t';
}

// token breaks come from the template alone, so a path with spaces or shell
// metacharacters always stays one argv element
bool matuwall_command_expand(
	struct matuwall_command *cmd, const char *tmpl, const char *path) {
	size_t path_len = strlen(path);
	size_t used = 0;
	cmd->argc = 0;
	cmd->argv[0] = NULL;

	const char *p = tmpl;
	for (;;) {
		while (is_space(*p)) {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		if (cmd->argc >= MATUWALL_COMMAND_MAX_ARGS) {
			return false;
		}
		cmd->argv[cmd->argc++] = cmd->buf + used;
		while (*p != '\0' && !is_space(*p)) {
			const char *piece = p;
			size_t len = 1;
			if (strncmp(p, "{path}", 6) == 0) {
				piece = path;
				len = path_len;
				p += 6;
			} else {
				p++;
			}
			// keep one byte free for the terminator
			if (len >= sizeof(cmd->buf) - used) {
				return false;
			}
			memcpy(cmd->buf + used, piece, len);
			used += len;
		}
		cmd->buf[used++] = '\0';
	}
	cmd->argv[cmd->argc] = NULL;
	return true;
}
