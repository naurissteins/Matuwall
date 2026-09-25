#ifndef MATUWALL_APP_THUMB_STORE_H
#define MATUWALL_APP_THUMB_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct matuwall_app;
struct matuwall_thumb_result;

bool matuwall_thumb_store_set_target(
	struct matuwall_app *app, uint32_t width, uint32_t height);

size_t matuwall_thumb_store_lookahead(
	const struct matuwall_app *app, size_t visible);

bool matuwall_thumb_store_in_window(const struct matuwall_app *app,
	size_t index, size_t first, size_t end, size_t wrap_end);

void matuwall_thumb_store_evict_outside(
	struct matuwall_app *app, size_t first, size_t end, size_t wrap_end);

void matuwall_thumb_store_accept(struct matuwall_app *app,
	const struct matuwall_thumb_result *result, size_t first, size_t end,
	size_t wrap_end);

#endif
