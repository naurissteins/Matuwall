#include "config/config.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "config/toml.h"

#define FIELD(member) offsetof(struct matuwall_config, member)

// rows run in --print-config order, string rows keep their capacity in max
const struct matuwall_config_key matuwall_config_keys[] = {
	{"general", "directory", MATUWALL_CONFIG_PATH, FIELD(directory), 0,
		PATH_MAX, "directory", NULL, 'd'},
	{"general", "backend", MATUWALL_CONFIG_BACKEND, FIELD(backend), 0, 32,
		"backend", NULL, 'b'},
	{"backend.sweetbg", "args", MATUWALL_CONFIG_ARGS, FIELD(sweetbg_args),
		0, 0, NULL, NULL, 0},
	{"backend.awww", "args", MATUWALL_CONFIG_ARGS, FIELD(awww_args), 0, 0,
		NULL, NULL, 0},
	{"backend.plasma", "args", MATUWALL_CONFIG_ARGS, FIELD(plasma_args), 0,
		0, NULL, NULL, 0},
	{"backend.command", "apply", MATUWALL_CONFIG_COMMAND,
		FIELD(backend_command), 0, MATUWALL_BACKEND_COMMAND_MAX, NULL,
		NULL, 0},
	{"window", "preview", MATUWALL_CONFIG_BOOL, FIELD(preview), 0, 0,
		"preview", "no-preview", 0},
	{"window", "close_on_focus_loss", MATUWALL_CONFIG_BOOL,
		FIELD(close_on_focus_loss), 0, 0, "close-on-focus-loss",
		"no-close-on-focus-loss", 0},
	{"window", "position", MATUWALL_CONFIG_POSITION, FIELD(position), 0, 0,
		"position", NULL, 'p'},
	{"window", "background", MATUWALL_CONFIG_COLOR, FIELD(background), 0, 0,
		"background", NULL, 0},
	{"window", "margin", MATUWALL_CONFIG_UINT, FIELD(layout.margin), 0,
		4096, "margin", NULL, 'm'},
	{"window", "edge_margin", MATUWALL_CONFIG_UINT, FIELD(edge_margin), 0,
		4096, "edge-margin", NULL, 0},
	{"window", "radius", MATUWALL_CONFIG_UINT, FIELD(panel_radius), 0, 4096,
		"panel-radius", NULL, 0},
	{"input", "mouse", MATUWALL_CONFIG_BOOL, FIELD(mouse_enabled), 0, 0,
		NULL, NULL, 0},
	{"animation", "navigation_ms", MATUWALL_CONFIG_UINT,
		FIELD(navigation_ms), 0, 1000, "navigation-ms", NULL, 0},
	{"animation", "zoom_percent", MATUWALL_CONFIG_UINT, FIELD(zoom_percent),
		0, 10, "zoom-percent", NULL, 0},
	{"grid", "columns", MATUWALL_CONFIG_UINT, FIELD(layout.columns), 1,
		1024, "columns", NULL, 'c'},
	{"grid", "visible_rows", MATUWALL_CONFIG_UINT, FIELD(visible_rows), 1,
		1024, "rows", NULL, 'r'},
	{"grid", "carousel", MATUWALL_CONFIG_BOOL, FIELD(carousel), 0, 0,
		"carousel", "no-carousel", 0},
	{"grid", "edge", MATUWALL_CONFIG_EDGE, FIELD(edge), 0, 0, "edge", NULL,
		0},
	{"grid", "spacing", MATUWALL_CONFIG_UINT, FIELD(layout.spacing), 0,
		4096, "spacing", NULL, 's'},
	{"grid", "radius", MATUWALL_CONFIG_UINT, FIELD(layout.radius), 0, 4096,
		"radius", NULL, 0},
	{"grid", "border_width", MATUWALL_CONFIG_UINT, FIELD(border_width), 0,
		4096, "border-width", NULL, 0},
	{"grid", "shadow_width", MATUWALL_CONFIG_UINT, FIELD(shadow_width), 0,
		4096, "shadow-width", NULL, 0},
	{"grid", "ring_width", MATUWALL_CONFIG_UINT, FIELD(ring_width), 0, 4096,
		"ring-width", NULL, 0},
	{"thumbnail", "width", MATUWALL_CONFIG_UINT, FIELD(layout.tile_width),
		1, 16384, "width", NULL, 'w'},
	{"thumbnail", "height", MATUWALL_CONFIG_UINT, FIELD(layout.tile_height),
		1, 16384, "height", NULL, 0},
	{"colors", "tile", MATUWALL_CONFIG_COLOR, FIELD(tile), 0, 0, "tile",
		NULL, 0},
	{"colors", "border", MATUWALL_CONFIG_COLOR, FIELD(border), 0, 0,
		"border", NULL, 0},
	{"colors", "shadow", MATUWALL_CONFIG_COLOR, FIELD(shadow), 0, 0,
		"shadow", NULL, 0},
	{"colors", "ring", MATUWALL_CONFIG_COLOR, FIELD(ring), 0, 0, "ring",
		NULL, 0},
	{"colors", "spinner", MATUWALL_CONFIG_COLOR, FIELD(spinner), 0, 0,
		"spinner", NULL, 0},
	{"hooks", "on_apply", MATUWALL_CONFIG_HOOKS, FIELD(on_apply), 0, 0,
		NULL, NULL, 0},
};

const size_t matuwall_config_key_count =
	sizeof(matuwall_config_keys) / sizeof(matuwall_config_keys[0]);

_Static_assert(sizeof(matuwall_config_keys) / sizeof(matuwall_config_keys[0]) <=
		       MATUWALL_CONFIG_MAX_KEYS,
	"raise MATUWALL_CONFIG_MAX_KEYS");

static bool set_string(char *out, size_t capacity,
	const struct matuwall_config_key *key, const char *value) {
	size_t len = strlen(value);
	if (len >= capacity) {
		return false;
	}
	switch (key->type) {
	case MATUWALL_CONFIG_PATH:
		return matuwall_config_expand_path(value, out, capacity);
	case MATUWALL_CONFIG_BACKEND:
		if (len == 0) {
			return false;
		}
		break;
	case MATUWALL_CONFIG_COMMAND:
		// empty means unset, anything else must say where the path goes
		if (len != 0 && strstr(value, "{path}") == NULL) {
			return false;
		}
		break;
	default:
		return false;
	}
	memcpy(out, value, len + 1);
	return true;
}

bool matuwall_config_set(struct matuwall_config *cfg,
	const struct matuwall_config_key *key,
	const struct matuwall_toml_value *v) {
	union {
		uint32_t number;
		bool flag;
		struct matuwall_color color;
		enum matuwall_position position;
		enum matuwall_edge edge;
		char text[PATH_MAX];
	} value;
	size_t size;
	bool string = v->type == MATUWALL_TOML_STRING;
	switch (key->type) {
	case MATUWALL_CONFIG_UINT:
		if (v->type != MATUWALL_TOML_INTEGER || v->integer < key->min ||
			v->integer > key->max) {
			return false;
		}
		value.number = (uint32_t)v->integer;
		size = sizeof(value.number);
		break;
	case MATUWALL_CONFIG_BOOL:
		if (v->type != MATUWALL_TOML_BOOLEAN) {
			return false;
		}
		value.flag = v->boolean;
		size = sizeof(value.flag);
		break;
	case MATUWALL_CONFIG_COLOR:
		if (!string || !matuwall_color_parse(v->string, &value.color)) {
			return false;
		}
		size = sizeof(value.color);
		break;
	case MATUWALL_CONFIG_POSITION:
		if (!string || !matuwall_position_from_name(
				       v->string, &value.position)) {
			return false;
		}
		size = sizeof(value.position);
		break;
	case MATUWALL_CONFIG_EDGE:
		if (!string ||
			!matuwall_edge_from_name(v->string, &value.edge)) {
			return false;
		}
		size = sizeof(value.edge);
		break;
	case MATUWALL_CONFIG_PATH:
	case MATUWALL_CONFIG_BACKEND:
	case MATUWALL_CONFIG_COMMAND:
		if (!string || !set_string(value.text, (size_t)key->max, key,
				       v->string)) {
			return false;
		}
		size = strlen(value.text) + 1;
		break;
	default:
		return false;
	}
	if (cfg != NULL) {
		memcpy((char *)cfg + key->offset, &value, size);
	}
	return true;
}

bool matuwall_config_set_text(struct matuwall_config *cfg,
	const struct matuwall_config_key *key, const char *text) {
	struct matuwall_toml_value v = {
		.type = MATUWALL_TOML_STRING,
		.string = text,
	};
	if (key->type == MATUWALL_CONFIG_UINT) {
		// digits only: no sign, space, or base prefix
		if (text[0] < '0' || text[0] > '9') {
			return false;
		}
		errno = 0;
		char *end;
		long long parsed = strtoll(text, &end, 10);
		if (errno == ERANGE || *end != '\0') {
			return false;
		}
		v = (struct matuwall_toml_value){
			.type = MATUWALL_TOML_INTEGER,
			.integer = parsed,
		};
	} else if (key->type == MATUWALL_CONFIG_BOOL) {
		bool on = strcmp(text, "true") == 0;
		if (!on && strcmp(text, "false") != 0) {
			return false;
		}
		v = (struct matuwall_toml_value){
			.type = MATUWALL_TOML_BOOLEAN,
			.boolean = on,
		};
	}
	return matuwall_config_set(cfg, key, &v);
}

const char *matuwall_config_expect(
	const struct matuwall_config_key *key, char *buf, size_t size) {
	switch (key->type) {
	case MATUWALL_CONFIG_UINT:
		snprintf(buf, size, "%lld..%lld", (long long)key->min,
			(long long)key->max);
		return buf;
	case MATUWALL_CONFIG_BOOL:
		return "true or false";
	case MATUWALL_CONFIG_COLOR:
		return "\"#rrggbb\", \"#rrggbbaa\", or \"none\"";
	case MATUWALL_CONFIG_POSITION:
		return "\"center\", \"left\", \"right\", \"top\", or "
		       "\"bottom\"";
	case MATUWALL_CONFIG_EDGE:
		return "\"auto\", \"clip\", \"peek\", or \"fade\"";
	case MATUWALL_CONFIG_PATH:
		return "a path that can be resolved";
	case MATUWALL_CONFIG_BACKEND:
		return "\"sweetbg\", \"awww\", \"plasma\", \"command\", or "
		       "\"auto\"";
	case MATUWALL_CONFIG_COMMAND:
		return "empty or a command with {path}, up to 511 bytes";
	case MATUWALL_CONFIG_HOOKS:
		return "an array of command strings";
	case MATUWALL_CONFIG_ARGS:
		return "an array of strings";
	}
	return "a valid value";
}

void matuwall_config_format(const struct matuwall_config *cfg,
	const struct matuwall_config_key *key, char *out, size_t size) {
	const char *field = (const char *)cfg + key->offset;
	switch (key->type) {
	case MATUWALL_CONFIG_UINT:
		snprintf(out, size, "%u",
			*(const uint32_t *)(const void *)field);
		return;
	case MATUWALL_CONFIG_BOOL:
		snprintf(out, size, "%s",
			*(const bool *)field ? "true" : "false");
		return;
	case MATUWALL_CONFIG_COLOR: {
		const struct matuwall_color *c =
			(const struct matuwall_color *)(const void *)field;
		if (c->a == 0) {
			snprintf(out, size, "none");
		} else {
			snprintf(out, size, "#%02x%02x%02x%02x", c->r, c->g,
				c->b, c->a);
		}
		return;
	}
	case MATUWALL_CONFIG_POSITION:
		snprintf(out, size, "%s",
			matuwall_position_name(
				*(const enum matuwall_position *)(const void *)
					field));
		return;
	case MATUWALL_CONFIG_EDGE:
		snprintf(out, size, "%s",
			matuwall_edge_name(
				*(const enum matuwall_edge *)(const void *)
					field));
		return;
	case MATUWALL_CONFIG_PATH:
	case MATUWALL_CONFIG_BACKEND:
	case MATUWALL_CONFIG_COMMAND:
		snprintf(out, size, "%s", field);
		return;
	case MATUWALL_CONFIG_HOOKS:
	case MATUWALL_CONFIG_ARGS:
		break;
	}
	snprintf(out, size, "%s", "");
}
