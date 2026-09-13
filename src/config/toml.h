#ifndef MATUWALL_CONFIG_TOML_H
#define MATUWALL_CONFIG_TOML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum matuwall_toml_type {
	MATUWALL_TOML_STRING,
	MATUWALL_TOML_INTEGER,
	MATUWALL_TOML_BOOLEAN,
	MATUWALL_TOML_ARRAY,
};

struct matuwall_toml_value {
	enum matuwall_toml_type type;
	const char *string;	  // STRING
	int64_t integer;	  // INTEGER
	bool boolean;		  // BOOLEAN
	const char *const *items; // ARRAY: string elements
	size_t item_count;
};

typedef bool (*matuwall_toml_visitor)(void *user_data, const char *section,
	const char *key, const struct matuwall_toml_value *value, int line,
	char *err, size_t err_size);

bool matuwall_toml_parse(FILE *fp, const char *name,
	matuwall_toml_visitor visit, void *user_data, char *err,
	size_t err_size);

#endif
