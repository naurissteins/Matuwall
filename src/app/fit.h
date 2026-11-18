#ifndef MATUWALL_APP_FIT_H
#define MATUWALL_APP_FIT_H

#include "wayland/layer.h"

struct matuwall_app;

// fit the grid to the output and size the panel surface for it
struct matuwall_layer_panel matuwall_app_fit_panel(struct matuwall_app *app);

// refit the grid and place the panel inside its configured surface
void matuwall_app_fit_refresh(struct matuwall_app *app);

// follow a mode or scale change of the output while the picker is open
void matuwall_app_fit_follow(struct matuwall_app *app);

#endif
