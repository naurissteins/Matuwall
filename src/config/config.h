#ifndef SWEETWALL_CONFIG_CONFIG_H
#define SWEETWALL_CONFIG_CONFIG_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "grid/layout.h"
#include "render/color.h"

const char *sweetwall_position_name(enum sweetwall_position position);
bool sweetwall_position_from_name(
	const char *name, enum sweetwall_position *out);

#define SWEETWALL_MAX_HOOKS 16
#define SWEETWALL_HOOK_MAX 512

struct sweetwall_config {
	char directory[PATH_MAX];
	char backend[32];
	enum sweetwall_position position;
	struct sweetwall_color background;
	struct sweetwall_color tile;
	struct sweetwall_color border;
	struct sweetwall_color ring;
	struct sweetwall_color spinner;
	struct sweetwall_layout layout;
	uint32_t visible_rows;
	uint32_t panel_radius;
	uint32_t border_width;
	uint32_t ring_width;
	// Fill the output with the selected wallpaper behind the panel
	bool preview;
	// on_apply command templates, run after a successful apply
	char on_apply[SWEETWALL_MAX_HOOKS][SWEETWALL_HOOK_MAX];
	size_t on_apply_count;
};

void sweetwall_config_defaults(struct sweetwall_config *cfg);
bool sweetwall_config_expand_path(const char *in, char *out, size_t out_size);

bool sweetwall_config_load(
	struct sweetwall_config *cfg, char *err, size_t err_size);
bool sweetwall_config_load_path(struct sweetwall_config *cfg, const char *path,
	char *err, size_t err_size);
size_t sweetwall_config_warning_count(void);

bool sweetwall_config_path(char *out, size_t out_size);

#endif
