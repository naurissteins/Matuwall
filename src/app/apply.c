#include "app/apply.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "app/app.h"
#include "backend/backend.h"
#include "hooks/hooks.h"
#include "scan/dirscan.h"
#include "state/selection.h"
#include "util/command.h"
#include "util/log.h"

// configured backend flags with {path} and {output} filled in
static bool expand_args(struct matuwall_command *cmd,
	const struct matuwall_backend_args *extra, const char *path,
	const char *output, bool *needs_output) {
	const char *args[MATUWALL_MAX_BACKEND_ARGS];
	size_t count = extra != NULL ? extra->count : 0;
	*needs_output = false;
	for (size_t i = 0; i < count; i++) {
		args[i] = extra->items[i];
		*needs_output |= matuwall_command_needs_output(args[i]);
	}
	if (output == NULL && *needs_output) {
		matuwall_log_error("backend",
			"backend args use {output}, but no output is known; "
			"pass --output");
		return false;
	}
	if (!matuwall_command_expand_args(cmd, args, count, path, output)) {
		matuwall_log_error("backend", "backend args are too long");
		return false;
	}
	return true;
}

// Hand one scanned path to the backend, then remember it and run hooks
bool matuwall_apply_path(const struct matuwall_config *config, const char *path,
	const char *output, bool hooks) {
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
	struct matuwall_command args;
	bool targeted;
	if (!expand_args(&args,
		    matuwall_config_backend_args(config, backend->name),
		    resolved, output, &targeted)) {
		return false;
	}
	targeted |= backend == &matuwall_backend_command &&
		    matuwall_command_needs_output(config->backend_command);
	// opts borrows args and config strings for the exec
	struct matuwall_apply_opts opts = {
		.args = (const char *const *)args.argv,
		.arg_count = args.argc,
		.command = config->backend_command,
		.output = output,
	};
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
	// only an apply that used {output} left the other outputs alone
	if (!matuwall_selection_save(path, targeted ? output : NULL)) {
		matuwall_log_warn("state", "could not remember the selection");
	}
	matuwall_log_info("apply", "applied %s with %s%s%s", resolved,
		backend->name, targeted ? " on " : "", targeted ? output : "");
	if (hooks) {
		matuwall_hooks_run(config, resolved, output);
	}
	return true;
}

const char *matuwall_app_output(const struct matuwall_app *app) {
	const char *name =
		matuwall_outputs_entered_name(&app->registry.outputs);
	return name != NULL ? name : app->registry.requested_output_name;
}

bool matuwall_app_apply(struct matuwall_app *app) {
	return matuwall_apply_path(&app->config,
		app->scan.paths[app->grid.selected], matuwall_app_output(app),
		true);
}

// --- background queue ---

// Applies run one at a time, so a slow backend can't land out of order
static void *apply_thread(void *data) {
	struct matuwall_apply_queue *queue = data;
	char path[PATH_MAX];
	char output[MATUWALL_APPLY_OUTPUT_MAX];
	pthread_mutex_lock(&queue->lock);
	for (;;) {
		while (!queue->has_pending && !queue->stopping) {
			pthread_cond_wait(&queue->wake, &queue->lock);
		}
		if (!queue->has_pending) {
			break;
		}
		memcpy(path, queue->pending, sizeof(path));
		memcpy(output, queue->pending_output, sizeof(output));
		queue->has_pending = false;
		pthread_mutex_unlock(&queue->lock);

		// hooks are never waited on, reap the ones that already ended
		while (waitpid(-1, NULL, WNOHANG) > 0) {
		}
		bool ok = matuwall_apply_path(queue->config, path,
			output[0] != '\0' ? output : NULL, true);

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
	const char *output = matuwall_app_output(app);
	pthread_mutex_lock(&queue->lock);
	int written =
		snprintf(queue->pending, sizeof(queue->pending), "%s", path);
	int written_output =
		snprintf(queue->pending_output, sizeof(queue->pending_output),
			"%s", output != NULL ? output : "");
	bool fits = written >= 0 && (size_t)written < sizeof(queue->pending) &&
		    written_output >= 0 &&
		    (size_t)written_output < sizeof(queue->pending_output);
	queue->has_pending = fits;
	queue->failed |= !fits;
	pthread_cond_signal(&queue->wake);
	pthread_mutex_unlock(&queue->lock);
	if (!fits) {
		matuwall_log_error(
			"apply", "path or output name too long: %s", path);
	}
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
