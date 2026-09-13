#ifndef MATUWALL_CONFIG_STRING_H
#define MATUWALL_CONFIG_STRING_H

#include <stdbool.h>
#include <stddef.h>

bool matuwall_toml_string_parse(
	const char *value, char *out, size_t out_size, const char **end);

#endif
