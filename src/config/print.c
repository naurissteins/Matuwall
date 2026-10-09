#include "config/config.h"

#include <stdint.h>
#include <string.h>

static void print_quoted(FILE *out, const char *value) {
	fputc('"', out);
	for (const unsigned char *p = (const unsigned char *)value; *p != '\0';
		p++) {
		switch (*p) {
		case '"':
			fputs("\\\"", out);
			break;
		case '\\':
			fputs("\\\\", out);
			break;
		case '\b':
			fputs("\\b", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\f':
			fputs("\\f", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		default:
			if (*p < 0x20 || *p == 0x7f) {
				fprintf(out, "\\u%04x", *p);
			} else {
				fputc(*p, out);
			}
			break;
		}
	}
	fputc('"', out);
}

static void print_hooks(FILE *out, const struct matuwall_config *config) {
	fputs("[\n", out);
	for (size_t i = 0; i < config->on_apply_count; i++) {
		fputs("  ", out);
		print_quoted(out, config->on_apply[i]);
		fputs(",\n", out);
	}
	fputs("]", out);
}

static void print_args(FILE *out, const struct matuwall_backend_args *args) {
	fputc('[', out);
	for (size_t i = 0; i < args->count; i++) {
		fputs(i == 0 ? "" : ", ", out);
		print_quoted(out, args->items[i]);
	}
	fputc(']', out);
}

static void print_value(FILE *out, const struct matuwall_config *config,
	const struct matuwall_config_key *key) {
	char text[PATH_MAX];
	switch (key->type) {
	case MATUWALL_CONFIG_HOOKS:
		print_hooks(out, config);
		return;
	case MATUWALL_CONFIG_ARGS:
		print_args(out,
			(const struct matuwall_backend_args *)(const void
					*)((const char *)config + key->offset));
		return;
	case MATUWALL_CONFIG_UINT:
	case MATUWALL_CONFIG_BOOL:
		matuwall_config_format(config, key, text, sizeof(text));
		fputs(text, out);
		return;
	default:
		matuwall_config_format(config, key, text, sizeof(text));
		print_quoted(out, text);
		return;
	}
}

bool matuwall_config_print(FILE *out, const struct matuwall_config *config) {
	const char *section = NULL;
	for (size_t i = 0; i < matuwall_config_key_count; i++) {
		const struct matuwall_config_key *key =
			&matuwall_config_keys[i];
		if (section == NULL || strcmp(section, key->section) != 0) {
			fprintf(out, "%s[%s]\n", section == NULL ? "" : "\n",
				key->section);
			section = key->section;
		}
		fprintf(out, "%s = ", key->name);
		print_value(out, config, key);
		fputc('\n', out);
	}
	return ferror(out) == 0;
}
