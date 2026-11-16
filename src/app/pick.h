#ifndef MATUWALL_APP_PICK_H
#define MATUWALL_APP_PICK_H

#include <stdbool.h>

#include "config/config.h"

enum matuwall_pick {
	MATUWALL_PICK_RESTORE,
	MATUWALL_PICK_RANDOM,
	MATUWALL_PICK_NEXT,
	MATUWALL_PICK_PREV,
};

const char *matuwall_pick_flag(enum matuwall_pick pick);

// apply a wallpaper without a window, like Enter in the picker would
bool matuwall_pick_apply(
	const struct matuwall_config *config, enum matuwall_pick pick);

#endif
