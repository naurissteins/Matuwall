#ifndef MATUWALL_APP_PICK_H
#define MATUWALL_APP_PICK_H

#include <stdbool.h>
#include <stddef.h>

#include "config/config.h"

enum matuwall_pick {
	MATUWALL_PICK_RESTORE,
	MATUWALL_PICK_RANDOM,
	MATUWALL_PICK_NEXT,
	MATUWALL_PICK_PREV,
};

const char *matuwall_pick_flag(enum matuwall_pick pick);

// random index other than current when count > 1, current may be count
size_t matuwall_pick_random(size_t count, size_t current);

bool matuwall_pick_apply(const struct matuwall_config *config,
	enum matuwall_pick pick, const char *output);

#endif
