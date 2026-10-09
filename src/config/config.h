#ifndef MATUWALL_CONFIG_CONFIG_H
#define MATUWALL_CONFIG_CONFIG_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "grid/layout.h"
#include "render/color.h"

const char *matuwall_position_name(enum matuwall_position position);
bool matuwall_position_from_name(const char *name, enum matuwall_position *out);
const char *matuwall_edge_name(enum matuwall_edge edge);
bool matuwall_edge_from_name(const char *name, enum matuwall_edge *out);

#define MATUWALL_MAX_HOOKS 16
#define MATUWALL_HOOK_MAX 512
#define MATUWALL_MAX_BACKEND_ARGS 32
#define MATUWALL_BACKEND_ARG_MAX 256
#define MATUWALL_BACKEND_COMMAND_MAX 512

// extra flags from [backend.<name>] args, passed before the wallpaper path
struct matuwall_backend_args {
	char items[MATUWALL_MAX_BACKEND_ARGS][MATUWALL_BACKEND_ARG_MAX];
	size_t count;
};

struct matuwall_config {
	char directory[PATH_MAX];
	char backend[32];
	struct matuwall_backend_args sweetbg_args;
	struct matuwall_backend_args awww_args;
	struct matuwall_backend_args plasma_args;
	// [backend.command] apply template, empty when unset
	char backend_command[MATUWALL_BACKEND_COMMAND_MAX];
	enum matuwall_position position;
	uint32_t edge_margin;
	struct matuwall_color background;
	struct matuwall_color tile;
	struct matuwall_color border;
	struct matuwall_color shadow;
	struct matuwall_color ring;
	struct matuwall_color spinner;
	struct matuwall_layout layout;
	uint32_t visible_rows;
	bool carousel;
	enum matuwall_edge edge;
	uint32_t panel_radius;
	uint32_t border_width;
	uint32_t shadow_width;
	uint32_t ring_width;
	// Zero disables navigation transitions
	uint32_t navigation_ms;
	uint32_t zoom_percent;
	// Fill the output with the selected wallpaper behind the panel
	bool preview;
	bool close_on_focus_loss;
	bool mouse_enabled;
	// on_apply command templates, run after a successful apply
	char on_apply[MATUWALL_MAX_HOOKS][MATUWALL_HOOK_MAX];
	size_t on_apply_count;
};

void matuwall_config_defaults(struct matuwall_config *cfg);
// edge style to render, with "auto" settled by the background alpha
enum matuwall_edge matuwall_config_edge(const struct matuwall_config *cfg);
bool matuwall_config_expand_path(const char *in, char *out, size_t out_size);
// args for a backend name, NULL for "auto" or an unknown name
const struct matuwall_backend_args *matuwall_config_backend_args(
	const struct matuwall_config *cfg, const char *backend);

bool matuwall_config_load(
	struct matuwall_config *cfg, char *err, size_t err_size);
bool matuwall_config_load_path(struct matuwall_config *cfg, const char *path,
	char *err, size_t err_size);
size_t matuwall_config_warning_count(void);

bool matuwall_config_path(char *out, size_t out_size);

bool matuwall_config_print(FILE *out, const struct matuwall_config *config);

// --- key table ---

enum matuwall_config_type {
	MATUWALL_CONFIG_UINT,
	MATUWALL_CONFIG_BOOL,
	MATUWALL_CONFIG_COLOR,
	MATUWALL_CONFIG_POSITION,
	MATUWALL_CONFIG_EDGE,
	MATUWALL_CONFIG_PATH,
	MATUWALL_CONFIG_BACKEND,
	MATUWALL_CONFIG_COMMAND,
	MATUWALL_CONFIG_HOOKS,
	MATUWALL_CONFIG_ARGS,
};

// one config key: where TOML puts it, its field, and its command-line flags
struct matuwall_config_key {
	const char *section;
	const char *name;
	enum matuwall_config_type type;
	size_t offset;
	// integer bounds, or a string's capacity in max
	int64_t min;
	int64_t max;
	// long flag, NULL when the key is config only
	const char *flag;
	// boolean flag that sets false
	const char *off_flag;
	char short_flag;
};

#define MATUWALL_CONFIG_MAX_KEYS 48

extern const struct matuwall_config_key matuwall_config_keys[];
extern const size_t matuwall_config_key_count;

struct matuwall_toml_value;

// scalar keys only, cfg is untouched on false, and NULL only validates
bool matuwall_config_set(struct matuwall_config *cfg,
	const struct matuwall_config_key *key,
	const struct matuwall_toml_value *value);
// same, from command-line text; booleans take "true" or "false"
bool matuwall_config_set_text(struct matuwall_config *cfg,
	const struct matuwall_config_key *key, const char *text);
// what a valid value looks like, for messages
const char *matuwall_config_expect(
	const struct matuwall_config_key *key, char *buf, size_t size);
// a scalar value as config text, strings unquoted
void matuwall_config_format(const struct matuwall_config *cfg,
	const struct matuwall_config_key *key, char *out, size_t size);

#endif
