#ifndef SWEETWALL_CONFIG_TOML_H
#define SWEETWALL_CONFIG_TOML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum sweetwall_toml_type {
	SWEETWALL_TOML_STRING,
	SWEETWALL_TOML_INTEGER,
	SWEETWALL_TOML_BOOLEAN,
	SWEETWALL_TOML_ARRAY,
};

struct sweetwall_toml_value {
	enum sweetwall_toml_type type;
	const char *string;	  // STRING
	int64_t integer;	  // INTEGER
	bool boolean;		  // BOOLEAN
	const char *const *items; // ARRAY: string elements
	size_t item_count;
};

typedef bool (*sweetwall_toml_visitor)(void *user_data, const char *section,
	const char *key, const struct sweetwall_toml_value *value, int line,
	char *err, size_t err_size);

bool sweetwall_toml_parse(FILE *fp, const char *name,
	sweetwall_toml_visitor visit, void *user_data, char *err,
	size_t err_size);

#endif
