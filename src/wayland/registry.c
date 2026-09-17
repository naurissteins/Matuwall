#include "wayland/registry.h"

#include <string.h>

#include "fractional-scale-v1-client-protocol.h"
#include "util/log.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

// v6 gives wl_surface.preferred_buffer_scale, the integer-scale fallback
#define COMPOSITOR_MAX_VERSION 6
#define LAYER_SHELL_MAX_VERSION 4
#define SHM_VERSION 1
// v5 gives wl_seat.release and wl_keyboard.release
#define SEAT_MAX_VERSION 5
#define VIEWPORTER_VERSION 1
#define FRACTIONAL_SCALE_VERSION 1

static uint32_t min_u32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

static void handle_global(void *data, struct wl_registry *registry,
	uint32_t name, const char *interface, uint32_t version) {
	struct matuwall_registry *reg = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		if (reg->compositor == NULL) {
			reg->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface,
				min_u32(version, COMPOSITOR_MAX_VERSION));
		}
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		if (reg->shm == NULL) {
			reg->shm = wl_registry_bind(
				registry, name, &wl_shm_interface, SHM_VERSION);
		}
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		if (reg->seat == NULL) {
			reg->seat = wl_registry_bind(registry, name,
				&wl_seat_interface,
				min_u32(version, SEAT_MAX_VERSION));
		}
	} else if (reg->requested_output_name != NULL &&
		   strcmp(interface, wl_output_interface.name) == 0) {
		matuwall_outputs_bind(&reg->outputs, registry, name, version);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		if (reg->layer_shell == NULL) {
			reg->layer_shell = wl_registry_bind(registry, name,
				&zwlr_layer_shell_v1_interface,
				min_u32(version, LAYER_SHELL_MAX_VERSION));
		}
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
		if (reg->viewporter == NULL) {
			reg->viewporter = wl_registry_bind(registry, name,
				&wp_viewporter_interface, VIEWPORTER_VERSION);
		}
	} else if (strcmp(interface,
			   wp_fractional_scale_manager_v1_interface.name) ==
		   0) {
		if (reg->fractional_scale_manager == NULL) {
			reg->fractional_scale_manager = wl_registry_bind(
				registry, name,
				&wp_fractional_scale_manager_v1_interface,
				FRACTIONAL_SCALE_VERSION);
		}
	}
}

static void handle_global_remove(
	void *data, struct wl_registry *registry, uint32_t name) {
	struct matuwall_registry *reg = data;
	(void)registry;
	if (matuwall_outputs_remove(
		    &reg->outputs, name, reg->selected_output)) {
		reg->selected_output = NULL;
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static bool required_globals_available(const struct matuwall_registry *reg) {
	bool ok = true;
	if (reg->compositor == NULL) {
		matuwall_log_error(
			"wayland", "compositor does not expose wl_compositor");
		ok = false;
	}
	if (reg->shm == NULL) {
		matuwall_log_error(
			"wayland", "compositor does not expose wl_shm");
		ok = false;
	}
	if (reg->layer_shell == NULL) {
		matuwall_log_error("wayland",
			"compositor lacks wlr-layer-shell "
			"(zwlr_layer_shell_v1)");
		ok = false;
	}
	// Without a seat there would be no way to dismiss the picker
	if (reg->seat == NULL) {
		matuwall_log_error(
			"wayland", "compositor does not expose wl_seat");
		ok = false;
	}
	return ok;
}

bool matuwall_registry_init(struct matuwall_registry *reg,
	struct wl_display *display, const char *output_name) {
	*reg = (struct matuwall_registry){
		.requested_output_name = output_name,
	};

	reg->registry = wl_display_get_registry(display);
	if (reg->registry == NULL) {
		matuwall_log_error("wayland", "failed to get the registry");
		return false;
	}

	if (wl_registry_add_listener(reg->registry, &registry_listener, reg) <
		0) {
		matuwall_log_error(
			"wayland", "failed to listen for registry globals");
		return false;
	}

	if (wl_display_roundtrip(display) < 0) {
		matuwall_log_error("wayland", "registry roundtrip failed");
		return false;
	}

	return required_globals_available(reg);
}

bool matuwall_registry_select_output(
	struct matuwall_registry *reg, struct wl_display *display) {
	if (reg->requested_output_name == NULL) {
		return true;
	}
	// Bound globals can now deliver initial events to installed listeners
	if (wl_display_roundtrip(display) < 0) {
		matuwall_log_error(
			"wayland", "output discovery roundtrip failed");
		return false;
	}
	return matuwall_outputs_select(&reg->outputs,
		reg->requested_output_name, &reg->selected_output);
}

void matuwall_registry_finish(struct matuwall_registry *reg) {
	matuwall_outputs_finish(&reg->outputs);
	reg->selected_output = NULL;
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
	if (reg->seat != NULL) {
		if (wl_seat_get_version(reg->seat) >=
			WL_SEAT_RELEASE_SINCE_VERSION) {
			wl_seat_release(reg->seat);
		} else {
			wl_seat_destroy(reg->seat);
		}
		reg->seat = NULL;
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
