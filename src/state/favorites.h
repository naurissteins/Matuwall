#ifndef MATUWALL_STATE_FAVORITES_H
#define MATUWALL_STATE_FAVORITES_H

#include <stdbool.h>
#include <stddef.h>

// append marked paths the list file does not hold yet, as a --list reads them
bool matuwall_favorites_add(const char *file, const char *directory,
	char *const *paths, const bool *marked, size_t count);

#endif
