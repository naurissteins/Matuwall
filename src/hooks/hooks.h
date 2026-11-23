#ifndef MATUWALL_HOOKS_HOOKS_H
#define MATUWALL_HOOKS_HOOKS_H

#include "config/config.h"

// output fills {output}, NULL skips the hooks that use it
void matuwall_hooks_run(const struct matuwall_config *cfg, const char *path,
	const char *output);

#endif
