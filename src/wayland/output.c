#include "wayland/output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"

#define OUTPUT_MAX_VERSION 4

struct matuwall_output_entry {
	struct matuwall_outputs *owner;
	struct wl_output *proxy;
	uint32_t global_name;
	char *name;
	struct matuwall_output_entry *next;
};

static void handle_geometry(void *data, struct wl_output *output, int32_t x,
	int32_t y, int32_t physical_width, int32_t physical_height,
	int32_t subpixel, const char *make, const char *model,
	int32_t transform) {
	(void)data;
	(void)output;
	(void)x;
	(void)y;
	(void)physical_width;
	(void)physical_height;
	(void)subpixel;
	(void)make;
	(void)model;
	(void)transform;
}

static void handle_mode(void *data, struct wl_output *output, uint32_t flags,
	int32_t width, int32_t height, int32_t refresh) {
	(void)data;
	(void)output;
	(void)flags;
	(void)width;
	(void)height;
	(void)refresh;
}

static void handle_done(void *data, struct wl_output *output) {
	(void)data;
	(void)output;
}

static void handle_scale(void *data, struct wl_output *output, int32_t factor) {
	(void)data;
	(void)output;
	(void)factor;
}

static void handle_name(
	void *data, struct wl_output *output, const char *name) {
	(void)output;
	struct matuwall_output_entry *entry = data;
	char *copy = name == NULL ? NULL : strdup(name);
	if (copy == NULL) {
		entry->owner->failed = true;
		return;
	}
	free(entry->name);
	entry->name = copy;
}

static void handle_description(
	void *data, struct wl_output *output, const char *description) {
	(void)data;
	(void)output;
	(void)description;
}

static const struct wl_output_listener output_listener = {
	.geometry = handle_geometry,
	.mode = handle_mode,
	.done = handle_done,
	.scale = handle_scale,
	.name = handle_name,
	.description = handle_description,
};

static void destroy_entry(struct matuwall_output_entry *entry) {
	if (wl_output_get_version(entry->proxy) >=
		WL_OUTPUT_RELEASE_SINCE_VERSION) {
		wl_output_release(entry->proxy);
	} else {
		wl_output_destroy(entry->proxy);
	}
	free(entry->name);
	free(entry);
}

void matuwall_outputs_bind(struct matuwall_outputs *outputs,
	struct wl_registry *registry, uint32_t global_name, uint32_t version) {
	outputs->advertised = true;
	if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
		outputs->name_supported = true;
	}

	struct matuwall_output_entry *entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		outputs->failed = true;
		return;
	}
	entry->owner = outputs;
	entry->global_name = global_name;
	uint32_t bind_version =
		version < OUTPUT_MAX_VERSION ? version : OUTPUT_MAX_VERSION;
	entry->proxy = wl_registry_bind(
		registry, global_name, &wl_output_interface, bind_version);
	if (entry->proxy == NULL) {
		free(entry);
		outputs->failed = true;
		return;
	}
	if (wl_output_add_listener(entry->proxy, &output_listener, entry) < 0) {
		destroy_entry(entry);
		outputs->failed = true;
		return;
	}
	entry->next = outputs->head;
	outputs->head = entry;
}

bool matuwall_outputs_remove(struct matuwall_outputs *outputs,
	uint32_t global_name, const struct wl_output *selected) {
	struct matuwall_output_entry **link = &outputs->head;
	while (*link != NULL && (*link)->global_name != global_name) {
		link = &(*link)->next;
	}
	if (*link == NULL) {
		return false;
	}

	struct matuwall_output_entry *entry = *link;
	*link = entry->next;
	bool removed_selected = entry->proxy == selected;
	destroy_entry(entry);
	return removed_selected;
}

static void report_missing(
	const struct matuwall_outputs *outputs, const char *requested) {
	char names[512] = {0};
	size_t used = 0;
	bool has_name = false;
	bool truncated = false;
	for (const struct matuwall_output_entry *entry = outputs->head;
		entry != NULL; entry = entry->next) {
		if (entry->name == NULL) {
			continue;
		}
		has_name = true;
		int written = snprintf(names + used, sizeof(names) - used,
			"%s%s", used == 0 ? "" : ", ", entry->name);
		if (written < 0 || (size_t)written >= sizeof(names) - used) {
			truncated = true;
			break;
		}
		used += (size_t)written;
	}

	if (!outputs->advertised) {
		matuwall_log_error("output",
			"output '%s' not found; no outputs were advertised",
			requested);
	} else if (!outputs->name_supported) {
		matuwall_log_error("output",
			"cannot select '%s': compositor lacks wl_output.name",
			requested);
	} else if (!has_name) {
		matuwall_log_error("output",
			"output '%s' not found; no named outputs were "
			"advertised",
			requested);
	} else {
		matuwall_log_error("output",
			"output '%s' not found (available: %s%s)", requested,
			names, truncated ? "..." : "");
	}
}

bool matuwall_outputs_select(struct matuwall_outputs *outputs, const char *name,
	struct wl_output **selected) {
	if (outputs->failed) {
		matuwall_log_error(
			"output", "failed to discover Wayland outputs");
		return false;
	}
	for (struct matuwall_output_entry *entry = outputs->head; entry != NULL;
		entry = entry->next) {
		if (entry->name != NULL && strcmp(entry->name, name) == 0) {
			*selected = entry->proxy;
			matuwall_log_info("output", "selected %s", name);
			return true;
		}
	}
	report_missing(outputs, name);
	return false;
}

void matuwall_outputs_finish(struct matuwall_outputs *outputs) {
	while (outputs->head != NULL) {
		struct matuwall_output_entry *entry = outputs->head;
		outputs->head = entry->next;
		destroy_entry(entry);
	}
	*outputs = (struct matuwall_outputs){0};
}
