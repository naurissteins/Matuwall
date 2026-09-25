#ifndef MATUWALL_APP_THUMBS_H
#define MATUWALL_APP_THUMBS_H

struct matuwall_app;

void matuwall_app_thumbs_start(struct matuwall_app *app);

void matuwall_app_thumbs_prioritize_visible(struct matuwall_app *app);

void matuwall_app_thumbs_drain(struct matuwall_app *app);

// stop decoding and free thumbnails without waiting for running workers
void matuwall_app_thumbs_quiesce(struct matuwall_app *app);

void matuwall_app_thumbs_finish(struct matuwall_app *app);

#endif
