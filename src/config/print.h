#ifndef MATUWALL_CONFIG_PRINT_H
#define MATUWALL_CONFIG_PRINT_H

#include <stdbool.h>
#include <stdio.h>

#include "config/config.h"

bool matuwall_config_print(FILE *out, const struct matuwall_config *config);

#endif
