#include "app/pick.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include "app/apply.h"
#include "scan/dirscan.h"
#include "scan/order.h"
#include "state/selection.h"
#include "util/log.h"

const char *matuwall_pick_flag(enum matuwall_pick pick) {
	switch (pick) {
	case MATUWALL_PICK_RANDOM:
		return "--random";
	case MATUWALL_PICK_NEXT:
		return "--next";
	case MATUWALL_PICK_PREV:
		return "--prev";
	case MATUWALL_PICK_RESTORE:
		break;
	}
	return "--restore";
}

// modulo bias is irrelevant at wallpaper counts
static size_t random_below(size_t bound) {
	uint64_t value;
	if (getrandom(&value, sizeof(value), 0) != (ssize_t)sizeof(value)) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		value = (uint64_t)now.tv_nsec ^ (uint64_t)now.tv_sec ^
			(uint64_t)getpid();
	}
	return (size_t)(value % bound);
}

// current is count when the last wallpaper is not in this scan
static size_t choose(enum matuwall_pick pick, size_t count, size_t current) {
	bool found = current < count;
	switch (pick) {
	case MATUWALL_PICK_NEXT:
		return found ? (current + 1) % count : 0;
	case MATUWALL_PICK_PREV:
		return found ? (current + count - 1) % count : count - 1;
	case MATUWALL_PICK_RANDOM:
	case MATUWALL_PICK_RESTORE:
		break;
	}
	if (!found || count == 1) {
		return random_below(count);
	}
	// never the wallpaper already showing
	size_t index = random_below(count - 1);
	return index >= current ? index + 1 : index;
}

static size_t find(const struct matuwall_dirscan *scan, const char *path) {
	for (size_t i = 0; i < scan->count; i++) {
		if (strcmp(scan->paths[i], path) == 0) {
			return i;
		}
	}
	return scan->count;
}

// same order as the grid, so --next steps through what the picker shows
static bool scan_wallpapers(
	const struct matuwall_config *config, struct matuwall_dirscan *scan) {
	if (!matuwall_dirscan_run(scan, config->directory, config->recursive)) {
		matuwall_log_error(
			"wallpapers", "cannot scan %s", config->directory);
		return false;
	}
	if (scan->unavailable) {
		return false;
	}
	if (scan->count == 0) {
		matuwall_log_error(
			"wallpapers", "no wallpapers in %s", config->directory);
		return false;
	}
	if (config->sort == MATUWALL_SORT_COLOR) {
		struct matuwall_order order;
		if (!matuwall_order_by_color(&order, scan)) {
			matuwall_log_warn("order", "cannot sort by colour");
		}
		matuwall_order_finish(&order, scan);
	}
	return true;
}

static bool apply_step(
	const struct matuwall_config *config, enum matuwall_pick pick) {
	char last[PATH_MAX];
	if (!matuwall_selection_load(last, sizeof(last))) {
		last[0] = '\0';
	}
	struct matuwall_dirscan scan;
	if (!scan_wallpapers(config, &scan)) {
		matuwall_dirscan_finish(&scan);
		return false;
	}
	size_t index = choose(pick, scan.count, find(&scan, last));
	matuwall_log_info("pick", "%s chose %zu of %zu",
		matuwall_pick_flag(pick), index + 1, scan.count);
	bool ok = matuwall_apply_path(config, scan.paths[index]);
	matuwall_dirscan_finish(&scan);
	return ok;
}

bool matuwall_pick_apply(
	const struct matuwall_config *config, enum matuwall_pick pick) {
	if (pick != MATUWALL_PICK_RESTORE) {
		return apply_step(config, pick);
	}
	// the remembered path may sit outside today's directory
	char last[PATH_MAX];
	if (!matuwall_selection_load(last, sizeof(last))) {
		matuwall_log_error(
			"pick", "no remembered wallpaper to restore");
		return false;
	}
	return matuwall_apply_path(config, last);
}
