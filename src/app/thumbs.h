#ifndef MATUWALL_APP_THUMBS_H
#define MATUWALL_APP_THUMBS_H

struct matuwall_app;

// reorders the scan, so it runs before anything holds an index
void matuwall_app_thumbs_sort(struct matuwall_app *app);

void matuwall_app_thumbs_start(struct matuwall_app *app);

void matuwall_app_thumbs_prioritize_visible(struct matuwall_app *app);

void matuwall_app_thumbs_prioritize_spin(struct matuwall_app *app);

// follow a scale change, old pixels draw until their replacements land
void matuwall_app_thumbs_retarget(struct matuwall_app *app);

void matuwall_app_thumbs_drain(struct matuwall_app *app);

// stop decoding and free thumbnails without waiting for running workers
void matuwall_app_thumbs_quiesce(struct matuwall_app *app);

void matuwall_app_thumbs_finish(struct matuwall_app *app);

#endif
