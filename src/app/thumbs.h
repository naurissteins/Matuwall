#ifndef SWEETWALL_APP_THUMBS_H
#define SWEETWALL_APP_THUMBS_H

struct sweetwall_app;

void sweetwall_app_thumbs_start(struct sweetwall_app *app);

void sweetwall_app_thumbs_prioritize_visible(struct sweetwall_app *app);

void sweetwall_app_thumbs_drain(struct sweetwall_app *app);

void sweetwall_app_thumbs_finish(struct sweetwall_app *app);

#endif
