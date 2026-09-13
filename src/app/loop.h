#ifndef MATUWALL_APP_LOOP_H
#define MATUWALL_APP_LOOP_H

#include <stdbool.h>

// SIGINT/SIGTERM set the flag the loop polls; install before matuwall_app_run
bool matuwall_app_loop_install_signals(void);

#endif
