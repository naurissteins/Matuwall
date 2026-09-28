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

// split tmpl on whitespace, then put path in place of each {path}. False on
// overflow, argc 0 for an empty template
bool matuwall_command_expand(
	struct matuwall_command *cmd, const char *tmpl, const char *path);

#endif
