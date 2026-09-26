#include "config/config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/toml.h"
#include "util/log.h"

#define DEFAULT_DIRECTORY "Pictures/Wallpapers"

// Config loads before workers; this describes only the latest load
static size_t warning_count;

const char *matuwall_position_name(enum matuwall_position position) {
	switch (position) {
	case MATUWALL_POSITION_LEFT:
		return "left";
	case MATUWALL_POSITION_RIGHT:
		return "right";
	case MATUWALL_POSITION_TOP:
		return "top";
	case MATUWALL_POSITION_BOTTOM:
		return "bottom";
	case MATUWALL_POSITION_CENTER:
		break;
	}
	return "center";
}

bool matuwall_position_from_name(
	const char *name, enum matuwall_position *out) {
	if (strcmp(name, "center") == 0) {
		*out = MATUWALL_POSITION_CENTER;
	} else if (strcmp(name, "left") == 0) {
		*out = MATUWALL_POSITION_LEFT;
	} else if (strcmp(name, "right") == 0) {
		*out = MATUWALL_POSITION_RIGHT;
	} else if (strcmp(name, "top") == 0) {
		*out = MATUWALL_POSITION_TOP;
	} else if (strcmp(name, "bottom") == 0) {
		*out = MATUWALL_POSITION_BOTTOM;
	} else {
		return false;
	}
	return true;
}

static const char *const edge_names[] = {
	[MATUWALL_EDGE_CLIP] = "clip",
	[MATUWALL_EDGE_PEEK] = "peek",
	[MATUWALL_EDGE_FADE] = "fade",
	[MATUWALL_EDGE_AUTO] = "auto",
};

const char *matuwall_edge_name(enum matuwall_edge edge) {
	if ((size_t)edge >= sizeof(edge_names) / sizeof(edge_names[0])) {
		return edge_names[MATUWALL_EDGE_AUTO];
	}
	return edge_names[edge];
}

bool matuwall_edge_from_name(const char *name, enum matuwall_edge *out) {
	for (size_t i = 0; i < sizeof(edge_names) / sizeof(edge_names[0]);
		i++) {
		if (strcmp(name, edge_names[i]) == 0) {
			*out = (enum matuwall_edge)i;
			return true;
		}
	}
	return false;
}

enum matuwall_edge matuwall_config_edge(const struct matuwall_config *cfg) {
	if (cfg->edge != MATUWALL_EDGE_AUTO) {
		return cfg->edge;
	}
	// an invisible panel edge makes a hard clip look like tiles vanish
	return cfg->background.a == 0 ? MATUWALL_EDGE_FADE : MATUWALL_EDGE_CLIP;
}

void matuwall_config_defaults(struct matuwall_config *cfg) {
	*cfg = (struct matuwall_config){
		.backend = "awww",
		.position = MATUWALL_POSITION_BOTTOM,
		.edge_margin = 24,
		.background = {.r = 0x11, .g = 0x11, .b = 0x1b, .a = 0xff},
		.tile = {.r = 0x31, .g = 0x32, .b = 0x44, .a = 0xff},
		.border = {.r = 0x58, .g = 0x5b, .b = 0x70, .a = 0xff},
		.shadow = {.r = 0x00, .g = 0x00, .b = 0x00, .a = 0x66},
		.ring = {.r = 0xf2, .g = 0xcd, .b = 0xcd, .a = 0xff},
		.spinner = {.r = 0xcd, .g = 0xd0, .b = 0xe6, .a = 0xff},
		.layout = {.columns = 5,
			.spacing = 16,
			.margin = 34,
			.tile_width = 300,
			.tile_height = 169,
			.radius = 20},
		.visible_rows = 1,
		.carousel = true,
		.edge = MATUWALL_EDGE_AUTO,
		.panel_radius = 40,
		.border_width = 0,
		.shadow_width = 12,
		.ring_width = 0,
		.navigation_ms = 410,
		.zoom_percent = 10,
		.preview = false,
		.close_on_focus_loss = false,
		.mouse_enabled = false,
	};

	const char *home = getenv("HOME");
	if (home != NULL) {
		snprintf(cfg->directory, sizeof(cfg->directory), "%s/%s", home,
			DEFAULT_DIRECTORY);
	}
}

// --- schema application ---

static void warn(int line, const char *detail) {
	warning_count++;
	matuwall_log_warn("config", "line %d: %s; using default", line, detail);
}

static void apply_string(char *dst, size_t size,
	const struct matuwall_toml_value *v, int line, const char *what) {
	if (v->type != MATUWALL_TOML_STRING) {
		warn(line, what);
		return;
	}
	char tmp[PATH_MAX];
	if (!matuwall_config_expand_path(v->string, tmp, sizeof(tmp)) ||
		strlen(tmp) >= size) {
		warn(line, what);
		return;
	}
	memcpy(dst, tmp, strlen(tmp) + 1);
}

// A bare name (no path expansion), e.g. the backend identifier
static void apply_name(char *dst, size_t size,
	const struct matuwall_toml_value *v, int line, const char *what) {
	if (v->type != MATUWALL_TOML_STRING || v->string[0] == '\0' ||
		strlen(v->string) >= size) {
		warn(line, what);
		return;
	}
	memcpy(dst, v->string, strlen(v->string) + 1);
}

static void apply_color(struct matuwall_color *dst,
	const struct matuwall_toml_value *v, int line, const char *what) {
	if (v->type != MATUWALL_TOML_STRING ||
		!matuwall_color_parse(v->string, dst)) {
		warn(line, what);
	}
}

static void apply_uint(uint32_t *dst, const struct matuwall_toml_value *v,
	int line, const char *what, int64_t lo, int64_t hi) {
	if (v->type != MATUWALL_TOML_INTEGER || v->integer < lo ||
		v->integer > hi) {
		warn(line, what);
		return;
	}
	*dst = (uint32_t)v->integer;
}

static void apply_bool(bool *dst, const struct matuwall_toml_value *v, int line,
	const char *what) {
	if (v->type != MATUWALL_TOML_BOOLEAN) {
		warn(line, what);
		return;
	}
	*dst = v->boolean;
}

static void apply_position(enum matuwall_position *dst,
	const struct matuwall_toml_value *v, int line) {
	if (v->type != MATUWALL_TOML_STRING ||
		!matuwall_position_from_name(v->string, dst)) {
		warn(line, "position must be \"center\", \"left\", \"right\", "
			   "\"top\", or \"bottom\"");
	}
}

static void apply_edge(enum matuwall_edge *dst,
	const struct matuwall_toml_value *v, int line) {
	if (v->type != MATUWALL_TOML_STRING ||
		!matuwall_edge_from_name(v->string, dst)) {
		warn(line, "edge must be \"auto\", \"clip\", \"peek\", or "
			   "\"fade\"");
	}
}

// TODO: drop the edge_peek alias after a couple of releases
static void apply_edge_peek(enum matuwall_edge *dst,
	const struct matuwall_toml_value *v, int line) {
	if (v->type != MATUWALL_TOML_BOOLEAN) {
		warn(line, "edge_peek must be true or false");
		return;
	}
	warning_count++;
	matuwall_log_warn("config",
		"line %d: edge_peek is deprecated; use edge = \"peek\" or "
		"\"clip\"",
		line);
	// An explicit edge wins wherever it appears
	if (v->boolean && *dst == MATUWALL_EDGE_AUTO) {
		*dst = MATUWALL_EDGE_PEEK;
	}
}

static void apply_hooks(struct matuwall_config *cfg,
	const struct matuwall_toml_value *v, int line) {
	if (v->type != MATUWALL_TOML_ARRAY) {
		warn(line, "on_apply must be an array of command strings");
		return;
	}
	cfg->on_apply_count = 0;
	for (size_t i = 0; i < v->item_count; i++) {
		if (cfg->on_apply_count >= MATUWALL_MAX_HOOKS) {
			warn(line, "too many on_apply hooks; extra ignored");
			return;
		}
		const char *cmd = v->items[i];
		if (strlen(cmd) >= MATUWALL_HOOK_MAX) {
			warn(line, "on_apply command too long; skipped");
			continue;
		}
		memcpy(cfg->on_apply[cfg->on_apply_count], cmd,
			strlen(cmd) + 1);
		cfg->on_apply_count++;
	}
}

// each item stays one argv entry, an empty one would reach the daemon as a path
static void apply_backend_args(struct matuwall_backend_args *args,
	const struct matuwall_toml_value *v, int line) {
	if (v->type != MATUWALL_TOML_ARRAY) {
		warn(line, "args must be an array of strings");
		return;
	}
	args->count = 0;
	for (size_t i = 0; i < v->item_count; i++) {
		if (args->count >= MATUWALL_MAX_BACKEND_ARGS) {
			warn(line, "too many backend args; extra ignored");
			return;
		}
		const char *arg = v->items[i];
		size_t len = strlen(arg);
		if (len == 0 || len >= MATUWALL_BACKEND_ARG_MAX) {
			warn(line, "backend arg empty or too long; skipped");
			continue;
		}
		memcpy(args->items[args->count], arg, len + 1);
		args->count++;
	}
}

static struct matuwall_backend_args *backend_args(
	struct matuwall_config *cfg, const char *backend) {
	if (strcmp(backend, "sweetbg") == 0) {
		return &cfg->sweetbg_args;
	}
	if (strcmp(backend, "awww") == 0) {
		return &cfg->awww_args;
	}
	return NULL;
}

const struct matuwall_backend_args *matuwall_config_backend_args(
	const struct matuwall_config *cfg, const char *backend) {
	// read-only view of the same lookup
	return backend_args((struct matuwall_config *)cfg, backend);
}

static bool unknown(const char *section, const char *key, int line, char *err,
	size_t err_size) {
	if (section[0] == '\0') {
		snprintf(err, err_size,
			"line %d: '%s' must be inside a [section]", line, key);
	} else {
		snprintf(err, err_size, "line %d: unknown key '%s' in [%s]",
			line, key, section);
	}
	return false;
}

static bool apply(void *user_data, const char *section, const char *key,
	const struct matuwall_toml_value *v, int line, char *err,
	size_t err_size) {
	struct matuwall_config *cfg = user_data;
	struct matuwall_backend_args *args =
		strncmp(section, "backend.", 8) == 0
			? backend_args(cfg, section + 8)
			: NULL;

	if (strcmp(section, "general") == 0) {
		if (strcmp(key, "directory") == 0) {
			apply_string(cfg->directory, sizeof(cfg->directory), v,
				line, "directory must be a string path");
			return true;
		}
		if (strcmp(key, "backend") == 0) {
			apply_name(cfg->backend, sizeof(cfg->backend), v, line,
				"backend must be \"sweetbg\", \"awww\", or "
				"\"auto\"");
			return true;
		}
	} else if (strcmp(section, "window") == 0) {
		if (strcmp(key, "position") == 0) {
			apply_position(&cfg->position, v, line);
			return true;
		}
		if (strcmp(key, "background") == 0) {
			apply_color(&cfg->background, v, line,
				"background must be \"#rrggbb\" or "
				"\"#rrggbbaa\"");
			return true;
		}
		if (strcmp(key, "margin") == 0) {
			apply_uint(&cfg->layout.margin, v, line,
				"margin must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "edge_margin") == 0) {
			apply_uint(&cfg->edge_margin, v, line,
				"edge_margin must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "radius") == 0) {
			apply_uint(&cfg->panel_radius, v, line,
				"window radius must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "preview") == 0) {
			apply_bool(&cfg->preview, v, line,
				"preview must be true or false");
			return true;
		}
		if (strcmp(key, "close_on_focus_loss") == 0) {
			apply_bool(&cfg->close_on_focus_loss, v, line,
				"close_on_focus_loss must be true or false");
			return true;
		}
	} else if (strcmp(section, "input") == 0) {
		if (strcmp(key, "mouse") == 0) {
			apply_bool(&cfg->mouse_enabled, v, line,
				"mouse must be true or false");
			return true;
		}
	} else if (strcmp(section, "animation") == 0) {
		if (strcmp(key, "navigation_ms") == 0) {
			apply_uint(&cfg->navigation_ms, v, line,
				"navigation_ms must be 0..1000", 0, 1000);
			return true;
		}
		if (strcmp(key, "zoom_percent") == 0) {
			apply_uint(&cfg->zoom_percent, v, line,
				"zoom_percent must be 0..10", 0, 10);
			return true;
		}
	} else if (strcmp(section, "grid") == 0) {
		if (strcmp(key, "columns") == 0) {
			apply_uint(&cfg->layout.columns, v, line,
				"columns must be 1..1024", 1, 1024);
			return true;
		}
		if (strcmp(key, "spacing") == 0) {
			apply_uint(&cfg->layout.spacing, v, line,
				"spacing must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "radius") == 0) {
			apply_uint(&cfg->layout.radius, v, line,
				"grid radius must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "border_width") == 0) {
			apply_uint(&cfg->border_width, v, line,
				"border_width must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "shadow_width") == 0) {
			apply_uint(&cfg->shadow_width, v, line,
				"shadow_width must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "ring_width") == 0) {
			apply_uint(&cfg->ring_width, v, line,
				"ring_width must be 0..4096", 0, 4096);
			return true;
		}
		if (strcmp(key, "visible_rows") == 0) {
			apply_uint(&cfg->visible_rows, v, line,
				"visible_rows must be 1..1024", 1, 1024);
			return true;
		}
		if (strcmp(key, "carousel") == 0) {
			apply_bool(&cfg->carousel, v, line,
				"carousel must be true or false");
			return true;
		}
		if (strcmp(key, "edge") == 0) {
			apply_edge(&cfg->edge, v, line);
			return true;
		}
		if (strcmp(key, "edge_peek") == 0) {
			apply_edge_peek(&cfg->edge, v, line);
			return true;
		}
	} else if (strcmp(section, "thumbnail") == 0) {
		if (strcmp(key, "width") == 0) {
			apply_uint(&cfg->layout.tile_width, v, line,
				"width must be 1..16384", 1, 16384);
			return true;
		}
		if (strcmp(key, "height") == 0) {
			apply_uint(&cfg->layout.tile_height, v, line,
				"height must be 1..16384", 1, 16384);
			return true;
		}
	} else if (strcmp(section, "colors") == 0) {
		if (strcmp(key, "tile") == 0) {
			apply_color(&cfg->tile, v, line,
				"tile must be \"#rrggbb\" or \"#rrggbbaa\"");
			return true;
		}
		if (strcmp(key, "border") == 0) {
			apply_color(&cfg->border, v, line,
				"border must be \"#rrggbb\" or \"#rrggbbaa\"");
			return true;
		}
		if (strcmp(key, "shadow") == 0) {
			apply_color(&cfg->shadow, v, line,
				"shadow must be \"#rrggbb\" or \"#rrggbbaa\"");
			return true;
		}
		if (strcmp(key, "ring") == 0) {
			apply_color(&cfg->ring, v, line,
				"ring must be \"#rrggbb\" or \"#rrggbbaa\"");
			return true;
		}
		if (strcmp(key, "spinner") == 0) {
			apply_color(&cfg->spinner, v, line,
				"spinner must be \"#rrggbb\" or \"#rrggbbaa\"");
			return true;
		}
	} else if (strcmp(section, "hooks") == 0) {
		if (strcmp(key, "on_apply") == 0) {
			apply_hooks(cfg, v, line);
			return true;
		}
	} else if (args != NULL) {
		if (strcmp(key, "args") == 0) {
			apply_backend_args(args, v, line);
			return true;
		}
	} else {
		snprintf(err, err_size, "line %d: unknown section [%s]", line,
			section);
		return false;
	}

	return unknown(section, key, line, err, err_size);
}

// --- loading ---

bool matuwall_config_path(char *out, size_t out_size) {
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		int n = snprintf(out, out_size, "%s/matuwall/config.toml", xdg);
		return n > 0 && (size_t)n < out_size;
	}
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		int n = snprintf(
			out, out_size, "%s/.config/matuwall/config.toml", home);
		return n > 0 && (size_t)n < out_size;
	}
	return false;
}

static bool load_path(struct matuwall_config *cfg, const char *path,
	bool missing_ok, char *err, size_t err_size) {
	warning_count = 0;
	matuwall_config_defaults(cfg);

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		if (missing_ok && errno == ENOENT) {
			matuwall_log_info(
				"config", "%s not found; using defaults", path);
			return true;
		}
		snprintf(err, err_size, "%s: %s", path, strerror(errno));
		return false;
	}

	bool ok = matuwall_toml_parse(fp, path, apply, cfg, err, err_size);
	fclose(fp);
	if (!ok) {
		matuwall_config_defaults(cfg);
	} else {
		matuwall_log_info("config", "loaded %s", path);
	}
	return ok;
}

bool matuwall_config_load(
	struct matuwall_config *cfg, char *err, size_t err_size) {
	char path[PATH_MAX];
	if (!matuwall_config_path(path, sizeof(path))) {
		warning_count = 0;
		matuwall_config_defaults(cfg);
		return true;
	}
	return load_path(cfg, path, true, err, err_size);
}

bool matuwall_config_load_path(struct matuwall_config *cfg, const char *path,
	char *err, size_t err_size) {
	return load_path(cfg, path, false, err, err_size);
}

size_t matuwall_config_warning_count(void) {
	return warning_count;
}
