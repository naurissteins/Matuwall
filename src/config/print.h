#ifndef SWEETWALL_CONFIG_PRINT_H
#define SWEETWALL_CONFIG_PRINT_H

#include <stdbool.h>
#include <stdio.h>

#include "config/config.h"

bool sweetwall_config_print(FILE *out, const struct sweetwall_config *config);

#endif
