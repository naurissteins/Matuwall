#ifndef SWEETWALL_APP_LOOP_H
#define SWEETWALL_APP_LOOP_H

#include <stdbool.h>

// SIGINT/SIGTERM set the flag the loop polls; install before sweetwall_app_run
bool sweetwall_app_loop_install_signals(void);

#endif
