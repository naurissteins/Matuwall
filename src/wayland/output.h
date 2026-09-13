#ifndef MATUWALL_WAYLAND_OUTPUT_H
#define MATUWALL_WAYLAND_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

struct matuwall_output_entry;

struct matuwall_outputs {
	struct matuwall_output_entry *head;
	bool advertised;
	bool name_supported;
	bool failed;
};

void matuwall_outputs_bind(struct matuwall_outputs *outputs,
	struct wl_registry *registry, uint32_t global_name, uint32_t version);

bool matuwall_outputs_remove(struct matuwall_outputs *outputs,
	uint32_t global_name, const struct wl_output *selected);

bool matuwall_outputs_select(struct matuwall_outputs *outputs, const char *name,
	struct wl_output **selected);

void matuwall_outputs_finish(struct matuwall_outputs *outputs);

#endif
