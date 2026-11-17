#include "app/apply.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "app/app.h"
#include "backend/backend.h"
#include "hooks/hooks.h"
#include "scan/dirscan.h"
#include "state/selection.h"
#include "util/log.h"

// Hand one scanned path to the backend, then remember it and run hooks
bool matuwall_apply_path(
	const struct matuwall_config *config, const char *path) {
	// state keeps the scanned path, backend and hooks get the real file
	char resolved[PATH_MAX];
	if (!matuwall_dirscan_resolve(path, resolved)) {
		return false;
	}
	const struct matuwall_backend *backend =
		matuwall_backend_select(config->backend);
	// config only holds known names, so NULL means auto found nothing
	if (backend == NULL) {
		matuwall_log_error(
			"backend", "no running wallpaper backend found");
		return false;
	}
	if (strcmp(config->backend, "auto") != 0) {
		matuwall_log_info("backend", "selected configured backend %s",
			backend->name);
	}
	// config owns the strings, opts only borrows them for the exec
	const struct matuwall_backend_args *extra =
		matuwall_config_backend_args(config, backend->name);
	const char *args[MATUWALL_MAX_BACKEND_ARGS];
	struct matuwall_apply_opts opts = {
		.args = args,
		.command = config->backend_command,
	};
	for (size_t i = 0; extra != NULL && i < extra->count; i++) {
		args[opts.arg_count++] = extra->items[i];
	}
	if (opts.arg_count > 0) {
		matuwall_log_info("backend",
			"passing %zu configured args to %s", opts.arg_count,
			backend->name);
	}
	if (!backend->apply(resolved, &opts)) {
		matuwall_log_error("backend",
			"%s failed to apply the wallpaper", backend->name);
		return false;
	}
	if (!matuwall_selection_save(path)) {
		matuwall_log_warn("state", "could not remember the selection");
	}
	matuwall_log_info(
		"apply", "applied %s with %s", resolved, backend->name);
	matuwall_hooks_run(config, resolved);
	return true;
}

bool matuwall_app_apply(struct matuwall_app *app) {
	return matuwall_apply_path(
		&app->config, app->scan.paths[app->grid.selected]);
}

// --- background queue ---

// Applies run one at a time, so a slow backend can't land out of order
static void *apply_thread(void *data) {
	struct matuwall_apply_queue *queue = data;
	char path[PATH_MAX];
	pthread_mutex_lock(&queue->lock);
	for (;;) {
		while (!queue->has_pending && !queue->stopping) {
			pthread_cond_wait(&queue->wake, &queue->lock);
		}
		if (!queue->has_pending) {
			break;
		}
		memcpy(path, queue->pending, sizeof(path));
		queue->has_pending = false;
		pthread_mutex_unlock(&queue->lock);

		// hooks are never waited on, reap the ones that already ended
		while (waitpid(-1, NULL, WNOHANG) > 0) {
		}
		bool ok = matuwall_apply_path(queue->config, path);

		pthread_mutex_lock(&queue->lock);
		queue->failed |= !ok;
	}
	pthread_mutex_unlock(&queue->lock);
	return NULL;
}

static bool start_thread(struct matuwall_apply_queue *queue,
	const struct matuwall_config *config) {
	queue->config = config;
	if (pthread_mutex_init(&queue->lock, NULL) != 0) {
		return false;
	}
	if (pthread_cond_init(&queue->wake, NULL) != 0) {
		pthread_mutex_destroy(&queue->lock);
		return false;
	}
	// inherits the loop's blocked signals, so only the loop sees them
	if (pthread_create(&queue->thread, NULL, apply_thread, queue) != 0) {
		pthread_cond_destroy(&queue->wake);
		pthread_mutex_destroy(&queue->lock);
		return false;
	}
	queue->started = true;
	return true;
}

void matuwall_app_apply_queue(struct matuwall_app *app, const char *path) {
	struct matuwall_apply_queue *queue = &app->apply;
	if (!queue->started && !start_thread(queue, &app->config)) {
		matuwall_log_error("apply", "cannot start the apply thread");
		return;
	}
	pthread_mutex_lock(&queue->lock);
	int written =
		snprintf(queue->pending, sizeof(queue->pending), "%s", path);
	queue->has_pending =
		written >= 0 && (size_t)written < sizeof(queue->pending);
	pthread_cond_signal(&queue->wake);
	pthread_mutex_unlock(&queue->lock);
}

bool matuwall_app_apply_finish(struct matuwall_app *app) {
	struct matuwall_apply_queue *queue = &app->apply;
	if (!queue->started) {
		return !queue->failed;
	}
	pthread_mutex_lock(&queue->lock);
	queue->stopping = true;
	pthread_cond_signal(&queue->wake);
	pthread_mutex_unlock(&queue->lock);
	pthread_join(queue->thread, NULL);
	pthread_cond_destroy(&queue->wake);
	pthread_mutex_destroy(&queue->lock);
	queue->started = false;
	return !queue->failed;
}
