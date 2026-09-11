#include "wayland/output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"

#define OUTPUT_MAX_VERSION 4

struct sweetwall_output_entry {
	struct sweetwall_outputs *owner;
	struct wl_output *proxy;
	uint32_t global_name;
	char *name;
	struct sweetwall_output_entry *next;
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
	struct sweetwall_output_entry *entry = data;
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

static void destroy_entry(struct sweetwall_output_entry *entry) {
	if (wl_output_get_version(entry->proxy) >=
		WL_OUTPUT_RELEASE_SINCE_VERSION) {
		wl_output_release(entry->proxy);
	} else {
		wl_output_destroy(entry->proxy);
	}
	free(entry->name);
	free(entry);
}

void sweetwall_outputs_bind(struct sweetwall_outputs *outputs,
	struct wl_registry *registry, uint32_t global_name, uint32_t version) {
	outputs->advertised = true;
	if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
		outputs->name_supported = true;
	}

	struct sweetwall_output_entry *entry = calloc(1, sizeof(*entry));
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

bool sweetwall_outputs_remove(struct sweetwall_outputs *outputs,
	uint32_t global_name, const struct wl_output *selected) {
	struct sweetwall_output_entry **link = &outputs->head;
	while (*link != NULL && (*link)->global_name != global_name) {
		link = &(*link)->next;
	}
	if (*link == NULL) {
		return false;
	}

	struct sweetwall_output_entry *entry = *link;
	*link = entry->next;
	bool removed_selected = entry->proxy == selected;
	destroy_entry(entry);
	return removed_selected;
}

static void report_missing(
	const struct sweetwall_outputs *outputs, const char *requested) {
	char names[512] = {0};
	size_t used = 0;
	bool has_name = false;
	bool truncated = false;
	for (const struct sweetwall_output_entry *entry = outputs->head;
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
		sweetwall_log_error("output",
			"output '%s' not found; no outputs were advertised",
			requested);
	} else if (!outputs->name_supported) {
		sweetwall_log_error("output",
			"cannot select '%s': compositor lacks wl_output.name",
			requested);
	} else if (!has_name) {
		sweetwall_log_error("output",
			"output '%s' not found; no named outputs were "
			"advertised",
			requested);
	} else {
		sweetwall_log_error("output",
			"output '%s' not found (available: %s%s)", requested,
			names, truncated ? "..." : "");
	}
}

bool sweetwall_outputs_select(struct sweetwall_outputs *outputs,
	const char *name, struct wl_output **selected) {
	if (outputs->failed) {
		sweetwall_log_error(
			"output", "failed to discover Wayland outputs");
		return false;
	}
	for (struct sweetwall_output_entry *entry = outputs->head;
		entry != NULL; entry = entry->next) {
		if (entry->name != NULL && strcmp(entry->name, name) == 0) {
			*selected = entry->proxy;
			sweetwall_log_info("output", "selected %s", name);
			return true;
		}
	}
	report_missing(outputs, name);
	return false;
}

void sweetwall_outputs_finish(struct sweetwall_outputs *outputs) {
	while (outputs->head != NULL) {
		struct sweetwall_output_entry *entry = outputs->head;
		outputs->head = entry->next;
		destroy_entry(entry);
	}
	*outputs = (struct sweetwall_outputs){0};
}
