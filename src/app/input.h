#ifndef SWEETWALL_APP_INPUT_H
#define SWEETWALL_APP_INPUT_H

#include "wayland/seat.h"

// Choose keyboard-only or mouse-enabled input policy
const struct sweetwall_seat_handler *sweetwall_app_input_handler(
	bool mouse_enabled);

#endif
