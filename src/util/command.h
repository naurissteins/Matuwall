#ifndef MATUWALL_UTIL_COMMAND_H
#define MATUWALL_UTIL_COMMAND_H

#include <stdbool.h>
#include <stddef.h>

#define MATUWALL_COMMAND_MAX_ARGS 64
#define MATUWALL_COMMAND_BUF 4096

// argv built from a command template, pointers borrow buf
struct matuwall_command {
	char buf[MATUWALL_COMMAND_BUF];
	char *argv[MATUWALL_COMMAND_MAX_ARGS + 1];
	size_t argc;
};

bool matuwall_command_expand(struct matuwall_command *cmd, const char *tmpl,
	const char *path, const char *output);
bool matuwall_command_expand_args(struct matuwall_command *cmd,
	const char *const *args, size_t count, const char *path,
	const char *output);
bool matuwall_command_needs_output(const char *tmpl);

#endif
