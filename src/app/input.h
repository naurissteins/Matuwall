#ifndef MATUWALL_APP_INPUT_H
#define MATUWALL_APP_INPUT_H

#include "wayland/seat.h"

// Choose keyboard-only or mouse-enabled input policy
const struct matuwall_seat_handler *matuwall_app_input_handler(
	bool mouse_enabled);

#endif
