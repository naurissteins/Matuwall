#include "config/toml.h"

#include <stdlib.h>
#include <string.h>

#include "config/string.h"

#define TOML_LINE_MAX 32768
#define TOML_SECTION_MAX 64
#define TOML_ARRAY_MAX_ITEMS 128
#define TOML_ARRAY_MAX_TEXT 65536

// Skip leading blanks and strip trailing blanks/newline in place
static char *trim(char *s) {
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	char *end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
				  end[-1] == '\r' || end[-1] == '\n')) {
		*--end = '\0';
	}
	return s;
}

static bool bare_token_ok(const char *after) {
	while (*after == ' ' || *after == '\t') {
		after++;
	}
	return *after == '\0' || *after == '#';
}

static bool parse_scalar(const char *value, struct sweetwall_toml_value *out) {
	if (strncmp(value, "true", 4) == 0 && bare_token_ok(value + 4)) {
		out->type = SWEETWALL_TOML_BOOLEAN;
		out->boolean = true;
		return true;
	}
	if (strncmp(value, "false", 5) == 0 && bare_token_ok(value + 5)) {
		out->type = SWEETWALL_TOML_BOOLEAN;
		out->boolean = false;
		return true;
	}

	char *endp = NULL;
	long long n = strtoll(value, &endp, 10);
	if (endp != value && bare_token_ok(endp)) {
		out->type = SWEETWALL_TOML_INTEGER;
		out->integer = (int64_t)n;
		return true;
	}
	return false;
}

static bool collect_array_text(char *text, size_t text_size, FILE *fp,
	const char *name, int *line, char *err, size_t err_size) {
	size_t len = strlen(text);
	int depth = 0;
	bool in_quote = false;
	size_t scan = 0;

	for (;;) {
		for (; scan < len; scan++) {
			char c = text[scan];
			if (in_quote) {
				if (c == '\\') {
					scan++;
				} else if (c == '"') {
					in_quote = false;
				}
			} else if (c == '"') {
				in_quote = true;
			} else if (c == '[') {
				depth++;
			} else if (c == ']') {
				if (--depth == 0) {
					return true;
				}
			}
		}

		char more[TOML_LINE_MAX];
		if (fgets(more, sizeof(more), fp) == NULL) {
			snprintf(err, err_size, "%s:%d: unterminated array",
				name, *line);
			return false;
		}
		(*line)++;
		char *piece = trim(more);
		if (len + 1 + strlen(piece) >= text_size) {
			snprintf(err, err_size, "%s:%d: array too large", name,
				*line);
			return false;
		}
		text[len++] = ' ';
		memcpy(text + len, piece, strlen(piece) + 1);
		len += strlen(piece);
	}
}

// Tokenize a collected "[ "a", "b" ]" body into item strings
static bool split_array(char *text, char **items, size_t *count,
	const char *name, int line, char *err, size_t err_size) {
	const char *p = text;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p != '[') {
		snprintf(err, err_size, "%s:%d: malformed array", name, line);
		return false;
	}
	p++;

	*count = 0;
	for (;;) {
		while (*p == ' ' || *p == '\t' || *p == ',') {
			p++;
		}
		if (*p == ']') {
			return true;
		}
		if (*p != '"') {
			snprintf(err, err_size,
				"%s:%d: array elements must be quoted strings",
				name, line);
			return false;
		}
		if (*count >= TOML_ARRAY_MAX_ITEMS) {
			snprintf(err, err_size,
				"%s:%d: too many array elements", name, line);
			return false;
		}
		char buffer[TOML_LINE_MAX];
		const char *end = NULL;
		if (!sweetwall_toml_string_parse(
			    p, buffer, sizeof(buffer), &end)) {
			snprintf(err, err_size, "%s:%d: unterminated string",
				name, line);
			return false;
		}
		char *item = malloc(strlen(buffer) + 1);
		if (item == NULL) {
			snprintf(err, err_size, "%s:%d: out of memory", name,
				line);
			return false;
		}
		memcpy(item, buffer, strlen(buffer) + 1);
		items[(*count)++] = item;
		p = end;
	}
}

static bool parse_array(char *value, FILE *fp, const char *name, int *line,
	struct sweetwall_toml_value *out, char **items, char *err,
	size_t err_size) {
	char text[TOML_ARRAY_MAX_TEXT];
	if (strlen(value) >= sizeof(text)) {
		snprintf(err, err_size, "%s:%d: array too large", name, *line);
		return false;
	}
	memcpy(text, value, strlen(value) + 1);

	if (!collect_array_text(
		    text, sizeof(text), fp, name, line, err, err_size)) {
		return false;
	}
	size_t count = 0;
	if (!split_array(text, items, &count, name, *line, err, err_size)) {
		for (size_t i = 0; i < count; i++) {
			free(items[i]);
		}
		return false;
	}
	out->type = SWEETWALL_TOML_ARRAY;
	out->items = (const char *const *)items;
	out->item_count = count;
	return true;
}

// Parse a "[section]" header into section, validating the close bracket
static bool parse_section(char *line, char *section, const char *name,
	int lineno, char *err, size_t err_size) {
	char *close = strchr(line, ']');
	const char *after = close != NULL ? close + 1 : NULL;
	while (after != NULL && (*after == ' ' || *after == '\t')) {
		after++;
	}
	if (close == NULL || (*after != '\0' && *after != '#')) {
		snprintf(err, err_size, "%s:%d: malformed section", name,
			lineno);
		return false;
	}
	*close = '\0';
	const char *inner = line + 1;
	if (inner[0] == '\0' || strlen(inner) >= TOML_SECTION_MAX) {
		snprintf(err, err_size, "%s:%d: invalid section name", name,
			lineno);
		return false;
	}
	memcpy(section, inner, strlen(inner) + 1);
	return true;
}

bool sweetwall_toml_parse(FILE *fp, const char *name,
	sweetwall_toml_visitor visit, void *user_data, char *err,
	size_t err_size) {
	char buffer[TOML_LINE_MAX];
	char section[TOML_SECTION_MAX] = "";
	int line = 0;

	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		line++;
		if (strchr(buffer, '\n') == NULL && feof(fp) == 0) {
			snprintf(err, err_size, "%s:%d: line too long", name,
				line);
			return false;
		}

		char *text = trim(buffer);
		if (*text == '\0' || *text == '#') {
			continue;
		}
		if (*text == '[') {
			if (!parse_section(
				    text, section, name, line, err, err_size)) {
				return false;
			}
			continue;
		}

		char *eq = strchr(text, '=');
		if (eq == NULL) {
			snprintf(err, err_size, "%s:%d: expected key = value",
				name, line);
			return false;
		}
		*eq = '\0';
		char *key = trim(text);
		char *value = trim(eq + 1);
		if (*key == '\0' || *value == '\0') {
			snprintf(err, err_size, "%s:%d: expected key = value",
				name, line);
			return false;
		}

		struct sweetwall_toml_value parsed = {0};
		char strbuf[TOML_LINE_MAX];
		char *items[TOML_ARRAY_MAX_ITEMS];
		bool is_array = false;

		if (*value == '"') {
			const char *after;
			if (!sweetwall_toml_string_parse(
				    value, strbuf, sizeof(strbuf), &after) ||
				!bare_token_ok(after)) {
				snprintf(err, err_size,
					"%s:%d: value must be a quoted string",
					name, line);
				return false;
			}
			parsed.type = SWEETWALL_TOML_STRING;
			parsed.string = strbuf;
		} else if (*value == '[') {
			is_array = true;
			if (!parse_array(value, fp, name, &line, &parsed, items,
				    err, err_size)) {
				return false;
			}
		} else if (!parse_scalar(value, &parsed)) {
			snprintf(err, err_size, "%s:%d: unrecognized value",
				name, line);
			return false;
		}

		bool ok = visit(
			user_data, section, key, &parsed, line, err, err_size);
		if (is_array) {
			for (size_t i = 0; i < parsed.item_count; i++) {
				free(items[i]);
			}
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}
