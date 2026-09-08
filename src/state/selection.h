#ifndef SWEETWALL_STATE_SELECTION_H
#define SWEETWALL_STATE_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

bool sweetwall_selection_load(char *path, size_t path_size);
bool sweetwall_selection_save(const char *path);

#endif
