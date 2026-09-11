#ifndef SWEETWALL_CONFIG_STRING_H
#define SWEETWALL_CONFIG_STRING_H

#include <stdbool.h>
#include <stddef.h>

bool sweetwall_toml_string_parse(
	const char *value, char *out, size_t out_size, const char **end);

#endif
