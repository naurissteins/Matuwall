#ifndef MATUWALL_SCAN_ORDER_H
#define MATUWALL_SCAN_ORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scan/dirscan.h"

struct matuwall_order_entry;

// colour keys kept in $XDG_CACHE_HOME/matuwall/colors between launches
struct matuwall_order {
	// one per scan entry, MATUWALL_HUE_UNKNOWN until a thumbnail lands
	uint32_t *keys;
	size_t count;
	size_t unknown;
	// stored entries borrow text, unused ones belong to other directories
	char *text;
	struct matuwall_order_entry *entries;
	size_t entry_count;
	bool dirty;
};

bool matuwall_order_by_color(
	struct matuwall_order *order, struct matuwall_dirscan *scan);

void matuwall_order_set(
	struct matuwall_order *order, size_t index, uint32_t key);

// writes changed keys for the next launch, then frees; scan is borrowed
void matuwall_order_finish(
	struct matuwall_order *order, const struct matuwall_dirscan *scan);

#endif
