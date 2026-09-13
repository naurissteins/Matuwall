#ifndef MATUWALL_WAYLAND_REGISTRY_H
#define MATUWALL_WAYLAND_REGISTRY_H

#include <stdbool.h>
#include <wayland-client.h>

#include "wayland/output.h"

struct zwlr_layer_shell_v1;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;

struct matuwall_registry {
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct matuwall_outputs outputs;
	struct wl_output *selected_output;
	const char *requested_output_name;
	struct zwlr_layer_shell_v1 *layer_shell;
	// Optional; together they give crisp output on fractional scales
	struct wp_viewporter *viewporter;
	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
};

// Bind the globals Matuwall needs. Reports which required global is missing
bool matuwall_registry_init(struct matuwall_registry *reg,
	struct wl_display *display, const char *output_name);

// Resolve an explicit output after listeners for other bound globals are ready
bool matuwall_registry_select_output(
	struct matuwall_registry *reg, struct wl_display *display);

// Destroy bound globals. Safe after a partial init
void matuwall_registry_finish(struct matuwall_registry *reg);

#endif
