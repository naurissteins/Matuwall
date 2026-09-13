#ifndef MATUWALL_STATE_SELECTION_H
#define MATUWALL_STATE_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

bool matuwall_selection_path(char *path, size_t path_size);
bool matuwall_selection_load(char *path, size_t path_size);
bool matuwall_selection_save(const char *path);

#endif
