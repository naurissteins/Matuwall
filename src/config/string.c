#include "config/string.h"

#include <stdint.h>
#include <string.h>

static int hex_value(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static bool append_utf8(
	uint32_t codepoint, char *out, size_t out_size, size_t *used) {
	unsigned char encoded[4];
	size_t count;
	if (codepoint == 0 || codepoint > 0x10ffff ||
		(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
		return false;
	}
	if (codepoint <= 0x7f) {
		encoded[0] = (unsigned char)codepoint;
		count = 1;
	} else if (codepoint <= 0x7ff) {
		encoded[0] = (unsigned char)(0xc0 | codepoint >> 6);
		encoded[1] = (unsigned char)(0x80 | (codepoint & 0x3f));
		count = 2;
	} else if (codepoint <= 0xffff) {
		encoded[0] = (unsigned char)(0xe0 | codepoint >> 12);
		encoded[1] = (unsigned char)(0x80 | (codepoint >> 6 & 0x3f));
		encoded[2] = (unsigned char)(0x80 | (codepoint & 0x3f));
		count = 3;
	} else {
		encoded[0] = (unsigned char)(0xf0 | codepoint >> 18);
		encoded[1] = (unsigned char)(0x80 | (codepoint >> 12 & 0x3f));
		encoded[2] = (unsigned char)(0x80 | (codepoint >> 6 & 0x3f));
		encoded[3] = (unsigned char)(0x80 | (codepoint & 0x3f));
		count = 4;
	}
	if (*used + count >= out_size) {
		return false;
	}
	memcpy(out + *used, encoded, count);
	*used += count;
	return true;
}

static bool parse_unicode(const char **cursor, size_t digits, char *out,
	size_t out_size, size_t *used) {
	uint32_t codepoint = 0;
	for (size_t i = 0; i < digits; i++) {
		int value = hex_value((*cursor)[i]);
		if (value < 0) {
			return false;
		}
		codepoint = codepoint << 4 | (uint32_t)value;
	}
	*cursor += digits;
	return append_utf8(codepoint, out, out_size, used);
}

static bool parse_escape(
	const char **cursor, char *out, size_t out_size, size_t *used) {
	char escaped = *(*cursor)++;
	switch (escaped) {
	case '"':
		return append_utf8('"', out, out_size, used);
	case '\\':
		return append_utf8('\\', out, out_size, used);
	case 'b':
		return append_utf8('\b', out, out_size, used);
	case 't':
		return append_utf8('\t', out, out_size, used);
	case 'n':
		return append_utf8('\n', out, out_size, used);
	case 'f':
		return append_utf8('\f', out, out_size, used);
	case 'r':
		return append_utf8('\r', out, out_size, used);
	case 'u':
		return parse_unicode(cursor, 4, out, out_size, used);
	case 'U':
		return parse_unicode(cursor, 8, out, out_size, used);
	default:
		return false;
	}
}

bool matuwall_toml_string_parse(
	const char *value, char *out, size_t out_size, const char **end) {
	if (value[0] != '"') {
		return false;
	}
	const char *cursor = value + 1;
	size_t used = 0;
	while (*cursor != '\0' && *cursor != '"') {
		unsigned char byte = (unsigned char)*cursor++;
		if (byte == '\\') {
			if (!parse_escape(&cursor, out, out_size, &used)) {
				return false;
			}
		} else if (byte < 0x20 || byte == 0x7f ||
			   used + 1 >= out_size) {
			return false;
		} else {
			out[used++] = (char)byte;
		}
	}
	if (*cursor != '"') {
		return false;
	}
	out[used] = '\0';
	if (end != NULL) {
		*end = cursor + 1;
	}
	return true;
}
