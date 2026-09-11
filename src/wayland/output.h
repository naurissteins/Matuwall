#ifndef SWEETWALL_WAYLAND_OUTPUT_H
#define SWEETWALL_WAYLAND_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

struct sweetwall_output_entry;

struct sweetwall_outputs {
	struct sweetwall_output_entry *head;
	bool advertised;
	bool name_supported;
	bool failed;
};

void sweetwall_outputs_bind(struct sweetwall_outputs *outputs,
	struct wl_registry *registry, uint32_t global_name, uint32_t version);

bool sweetwall_outputs_remove(struct sweetwall_outputs *outputs,
	uint32_t global_name, const struct wl_output *selected);

bool sweetwall_outputs_select(struct sweetwall_outputs *outputs,
	const char *name, struct wl_output **selected);

void sweetwall_outputs_finish(struct sweetwall_outputs *outputs);

#endif
