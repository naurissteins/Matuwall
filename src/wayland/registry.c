#include "wayland/registry.h"

#include <stdio.h>
#include <string.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// v6 gives wl_surface.preferred_buffer_scale, the integer-scale fallback
#define COMPOSITOR_MAX_VERSION 6
#define LAYER_SHELL_MAX_VERSION 4
#define SHM_VERSION 1
#define VIEWPORTER_VERSION 1
#define FRACTIONAL_SCALE_VERSION 1

static uint32_t min_u32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

static void handle_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version) {
	struct sweetwall_registry *reg = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		reg->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface,
			min_u32(version, COMPOSITOR_MAX_VERSION));
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		reg->shm = wl_registry_bind(
			registry, name, &wl_shm_interface, SHM_VERSION);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		reg->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface,
			min_u32(version, LAYER_SHELL_MAX_VERSION));
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
		reg->viewporter = wl_registry_bind(registry, name,
			&wp_viewporter_interface, VIEWPORTER_VERSION);
	} else if (strcmp(interface,
			   wp_fractional_scale_manager_v1_interface.name) ==
		   0) {
		reg->fractional_scale_manager = wl_registry_bind(registry, name,
			&wp_fractional_scale_manager_v1_interface,
			FRACTIONAL_SCALE_VERSION);
	}
}

static void handle_global_remove(
	void *data, struct wl_registry *registry, uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

bool sweetwall_registry_init(
	struct sweetwall_registry *reg, struct wl_display *display) {
	*reg = (struct sweetwall_registry){0};

	reg->registry = wl_display_get_registry(display);
	if (reg->registry == NULL) {
		fprintf(stderr,
			"sweetwall: failed to get the wayland registry\n");
		return false;
	}

	wl_registry_add_listener(reg->registry, &registry_listener, reg);

	if (wl_display_roundtrip(display) < 0) {
		fprintf(stderr, "sweetwall: wayland roundtrip failed\n");
		return false;
	}

	bool ok = true;
	if (reg->compositor == NULL) {
		fprintf(stderr, "sweetwall: compositor does not expose "
				"wl_compositor\n");
		ok = false;
	}
	if (reg->shm == NULL) {
		fprintf(stderr,
			"sweetwall: compositor does not expose wl_shm\n");
		ok = false;
	}
	if (reg->layer_shell == NULL) {
		fprintf(stderr, "sweetwall: compositor lacks wlr-layer-shell "
				"(zwlr_layer_shell_v1)\n");
		ok = false;
	}

	return ok;
}

void sweetwall_registry_finish(struct sweetwall_registry *reg) {
	if (reg->fractional_scale_manager != NULL) {
		wp_fractional_scale_manager_v1_destroy(
			reg->fractional_scale_manager);
		reg->fractional_scale_manager = NULL;
	}
	if (reg->viewporter != NULL) {
		wp_viewporter_destroy(reg->viewporter);
		reg->viewporter = NULL;
	}
	if (reg->layer_shell != NULL) {
		zwlr_layer_shell_v1_destroy(reg->layer_shell);
		reg->layer_shell = NULL;
	}
	if (reg->shm != NULL) {
		wl_shm_destroy(reg->shm);
		reg->shm = NULL;
	}
	if (reg->compositor != NULL) {
		wl_compositor_destroy(reg->compositor);
		reg->compositor = NULL;
	}
	if (reg->registry != NULL) {
		wl_registry_destroy(reg->registry);
		reg->registry = NULL;
	}
}
