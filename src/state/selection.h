#ifndef MATUWALL_STATE_SELECTION_H
#define MATUWALL_STATE_SELECTION_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#define MATUWALL_SELECTION_OUTPUTS 16
#define MATUWALL_SELECTION_NAME_MAX 128

// wallpapers applied to one output each, oldest first
struct matuwall_selection_outputs {
	size_t count;
	struct {
		char name[MATUWALL_SELECTION_NAME_MAX];
		char path[PATH_MAX];
	} items[MATUWALL_SELECTION_OUTPUTS];
};

bool matuwall_selection_path(char *path, size_t path_size);
// the last wallpaper applied anywhere
bool matuwall_selection_load(char *path, size_t path_size);
// output's own entry, else the global one, output may be NULL
bool matuwall_selection_load_for(
	const char *output, char *path, size_t path_size);
bool matuwall_selection_has_outputs(void);
// empty when missing or corrupt
void matuwall_selection_load_outputs(struct matuwall_selection_outputs *out);
// output NULL means every output got path, which drops per-output entries
bool matuwall_selection_save(const char *path, const char *output);

#endif
