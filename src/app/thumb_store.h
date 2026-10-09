#ifndef MATUWALL_APP_THUMB_STORE_H
#define MATUWALL_APP_THUMB_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct matuwall_app;
struct matuwall_thumb;
struct matuwall_thumb_result;

// every thumbnail field; release resets it in one assignment
struct matuwall_thumb_store {
	struct matuwall_thumb *items;
	size_t count;
	size_t visible_pending;
	size_t target_bytes;
	size_t resident_bytes;
	size_t resident_peak_bytes;
	// visible window the job queue was last reordered for
	size_t priority_first;
	size_t priority_end;
	size_t priority_wrap_end;
	bool priority_set;
	// exit summary counters
	size_t cache_hits;
	size_t decoded;
	// decoded but dropped outside the window, and withdrawn before running
	size_t discarded;
	size_t withdrawn;
	size_t evicted;
};

bool matuwall_thumb_store_set_target(
	struct matuwall_app *app, uint32_t width, uint32_t height);

// visible items as [first, end) plus a carousel wrap of [0, wrap_end)
void matuwall_thumb_store_visible_ranges(const struct matuwall_app *app,
	size_t *first, size_t *end, size_t *wrap_end);

bool matuwall_thumb_store_drawable(
	const struct matuwall_app *app, size_t index);

size_t matuwall_thumb_store_lookahead(
	const struct matuwall_app *app, size_t visible);

bool matuwall_thumb_store_in_window(const struct matuwall_app *app,
	size_t index, size_t first, size_t end, size_t wrap_end);

void matuwall_thumb_store_evict_outside(
	struct matuwall_app *app, size_t first, size_t end, size_t wrap_end);

void matuwall_thumb_store_accept(
	struct matuwall_app *app, const struct matuwall_thumb_result *result);

#endif
