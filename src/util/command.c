#include "util/command.h"

#include <string.h>

static bool is_space(char c) {
	return c == ' ' || c == '\t';
}

static bool starts_with(const char *p, const char *end, const char *word) {
	size_t len = strlen(word);
	return (size_t)(end - p) >= len && memcmp(p, word, len) == 0;
}

static bool add_token(struct matuwall_command *cmd, size_t *used, const char *p,
	size_t n, const char *path, const char *output) {
	if (cmd->argc >= MATUWALL_COMMAND_MAX_ARGS) {
		return false;
	}
	cmd->argv[cmd->argc++] = cmd->buf + *used;
	const char *end = p + n;
	while (p < end) {
		const char *piece = p;
		size_t len = 1;
		if (starts_with(p, end, "{path}")) {
			piece = path;
			len = strlen(path);
			p += 6;
		} else if (starts_with(p, end, "{output}")) {
			if (output == NULL) {
				return false;
			}
			piece = output;
			len = strlen(output);
			p += 8;
		} else {
			p++;
		}
		// keep one byte free for the terminator
		if (len >= sizeof(cmd->buf) - *used) {
			return false;
		}
		memcpy(cmd->buf + *used, piece, len);
		*used += len;
	}
	cmd->buf[(*used)++] = '\0';
	cmd->argv[cmd->argc] = NULL;
	return true;
}

// token breaks come from the template alone, so a path with spaces or shell
// metacharacters always stays one argv element
bool matuwall_command_expand(struct matuwall_command *cmd, const char *tmpl,
	const char *path, const char *output) {
	size_t used = 0;
	cmd->argc = 0;
	cmd->argv[0] = NULL;

	const char *p = tmpl;
	for (;;) {
		while (is_space(*p)) {
			p++;
		}
		if (*p == '\0') {
			return true;
		}
		const char *start = p;
		while (*p != '\0' && !is_space(*p)) {
			p++;
		}
		if (!add_token(cmd, &used, start, (size_t)(p - start), path,
			    output)) {
			return false;
		}
	}
}

bool matuwall_command_expand_args(struct matuwall_command *cmd,
	const char *const *args, size_t count, const char *path,
	const char *output) {
	size_t used = 0;
	cmd->argc = 0;
	cmd->argv[0] = NULL;
	for (size_t i = 0; i < count; i++) {
		if (!add_token(cmd, &used, args[i], strlen(args[i]), path,
			    output)) {
			return false;
		}
	}
	return true;
}

bool matuwall_command_needs_output(const char *tmpl) {
	return strstr(tmpl, "{output}") != NULL;
}
