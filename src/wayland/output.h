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
	// some output finished a mode, scale or transform change
	bool changed;
};

void matuwall_outputs_bind(struct matuwall_outputs *outputs,
	struct wl_registry *registry, uint32_t global_name, uint32_t version);

bool matuwall_outputs_remove(struct matuwall_outputs *outputs,
	uint32_t global_name, const struct wl_output *selected);

bool matuwall_outputs_select(struct matuwall_outputs *outputs, const char *name,
	struct wl_output **selected);

// tracks wl_surface enter and leave for the picker's surfaces
void matuwall_output_enter(struct wl_output *output, bool entered);

// the output the picker's surfaces are on, NULL before they map
struct wl_output *matuwall_outputs_entered(
	const struct matuwall_outputs *outputs);

void matuwall_outputs_finish(struct matuwall_outputs *outputs);

#endif
