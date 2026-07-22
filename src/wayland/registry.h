#ifndef SWEETWALL_WAYLAND_REGISTRY_H
#define SWEETWALL_WAYLAND_REGISTRY_H

#include <stdbool.h>
#include <wayland-client.h>

struct zwlr_layer_shell_v1;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;

struct sweetwall_registry {
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct zwlr_layer_shell_v1 *layer_shell;
	// Optional; together they give crisp output on fractional scales
	struct wp_viewporter *viewporter;
	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
};

// Bind the globals sweetwall needs. Reports which required global is missing
bool sweetwall_registry_init(
	struct sweetwall_registry *reg, struct wl_display *display);

// Destroy bound globals. Safe after a partial init
void sweetwall_registry_finish(struct sweetwall_registry *reg);

#endif
