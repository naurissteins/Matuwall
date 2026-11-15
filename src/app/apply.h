#ifndef MATUWALL_APP_APPLY_H
#define MATUWALL_APP_APPLY_H

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

#include "config/config.h"

struct matuwall_app;

// background applies while the picker stays open, latest request wins
struct matuwall_apply_queue {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t wake;
	// borrowed from the app, never written after init
	const struct matuwall_config *config;
	// pending, stopping and failed are guarded by lock
	char pending[PATH_MAX];
	bool has_pending;
	bool stopping;
	bool failed;
	// main thread only
	bool started;
};

// apply the selection and wait, for the closing path
bool matuwall_app_apply(struct matuwall_app *app);
// hand the selection to the apply thread, starting it on first use
void matuwall_app_apply_queue(struct matuwall_app *app);
// run what is still queued and join, false when any apply failed
bool matuwall_app_apply_finish(struct matuwall_app *app);

#endif
