#include "scan/order.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "thumb/hue.h"
#include "util/fs.h"
#include "util/log.h"

// bump when hue keys change meaning, an old file is then ignored
#define HEADER "matuwall-colors 1\n"
#define MAX_ENTRIES 8192
#define MAX_FILE_BYTES ((off_t)4 * 1024 * 1024)

struct matuwall_order_entry {
	const char *path;
	uint32_t key;
	bool used;
};

static bool index_path(char *out, size_t size) {
	return matuwall_fs_xdg_path(
		"XDG_CACHE_HOME", "/.cache", "/colors", out, size);
}

// --- load ---

static char *read_index(void) {
	char path[PATH_MAX];
	if (!index_path(path, sizeof(path))) {
		return NULL;
	}
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		return NULL;
	}
	struct stat info;
	char *text = NULL;
	if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
		info.st_size > 0 && info.st_size <= MAX_FILE_BYTES) {
		size_t size = (size_t)info.st_size;
		text = malloc(size + 1);
		// a NUL inside would silently shorten a path
		if (text != NULL && (!matuwall_fs_read_all(fd, text, size) ||
					    memchr(text, '\0', size) != NULL)) {
			free(text);
			text = NULL;
		} else if (text != NULL) {
			text[size] = '\0';
		}
	}
	close(fd);
	return text;
}

static bool parse_key(const char *hex, uint32_t *key) {
	uint32_t value = 0;
	for (size_t i = 0; i < 8; i++) {
		char c = hex[i];
		uint32_t digit;
		if (c >= '0' && c <= '9') {
			digit = (uint32_t)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			digit = (uint32_t)(c - 'a' + 10);
		} else {
			return false;
		}
		value = value << 4 | digit;
	}
	*key = value;
	return value != MATUWALL_HUE_UNKNOWN;
}

static int compare_entries(const void *a, const void *b) {
	const struct matuwall_order_entry *lhs = a;
	const struct matuwall_order_entry *rhs = b;
	return strcmp(lhs->path, rhs->path);
}

static void parse_index(struct matuwall_order *order) {
	char *line = order->text;
	if (strncmp(line, HEADER, strlen(HEADER)) != 0) {
		return;
	}
	line += strlen(HEADER);
	size_t lines = 0;
	for (const char *c = line; *c != '\0'; c++) {
		lines += *c == '\n';
	}
	if (lines > MAX_ENTRIES) {
		lines = MAX_ENTRIES;
	}
	order->entries = calloc(lines + 1, sizeof(*order->entries));
	if (order->entries == NULL) {
		return;
	}

	while (*line != '\0' && order->entry_count < lines) {
		char *end = strchr(line, '\n');
		if (end == NULL) {
			break;
		}
		*end = '\0';
		uint32_t key;
		if (end - line > 9 && line[8] == ' ' && parse_key(line, &key)) {
			struct matuwall_order_entry entry = {
				.path = line + 9,
				.key = key,
			};
			order->entries[order->entry_count++] = entry;
		}
		line = end + 1;
	}

	qsort(order->entries, order->entry_count, sizeof(*order->entries),
		compare_entries);
	size_t kept = 0;
	for (size_t i = 0; i < order->entry_count; i++) {
		if (kept == 0 || strcmp(order->entries[kept - 1].path,
					 order->entries[i].path) != 0) {
			order->entries[kept++] = order->entries[i];
		}
	}
	order->entry_count = kept;
}

static uint32_t stored_key(struct matuwall_order *order, const char *path) {
	if (order->entry_count == 0) {
		return MATUWALL_HUE_UNKNOWN;
	}
	struct matuwall_order_entry probe = {.path = path};
	struct matuwall_order_entry *entry = bsearch(&probe, order->entries,
		order->entry_count, sizeof(*order->entries), compare_entries);
	if (entry == NULL) {
		return MATUWALL_HUE_UNKNOWN;
	}
	entry->used = true;
	return entry->key;
}

// --- sort ---

struct ranked {
	char *path;
	uint32_t key;
	size_t rank;
};

// rank is the name position, so equal keys stay in name order
static int compare_ranked(const void *a, const void *b) {
	const struct ranked *lhs = a;
	const struct ranked *rhs = b;
	if (lhs->key != rhs->key) {
		return lhs->key < rhs->key ? -1 : 1;
	}
	return lhs->rank < rhs->rank ? -1 : lhs->rank > rhs->rank;
}

static bool sort_scan(
	struct matuwall_order *order, struct matuwall_dirscan *scan) {
	struct ranked *items = calloc(scan->count, sizeof(*items));
	if (items == NULL) {
		return false;
	}
	for (size_t i = 0; i < scan->count; i++) {
		items[i] = (struct ranked){
			.path = scan->paths[i],
			.key = order->keys[i],
			.rank = i,
		};
	}
	qsort(items, scan->count, sizeof(*items), compare_ranked);
	for (size_t i = 0; i < scan->count; i++) {
		scan->paths[i] = items[i].path;
		order->keys[i] = items[i].key;
	}
	free(items);
	return true;
}

bool matuwall_order_by_color(
	struct matuwall_order *order, struct matuwall_dirscan *scan) {
	*order = (struct matuwall_order){0};
	if (scan->count == 0) {
		return true;
	}
	order->keys = malloc(scan->count * sizeof(*order->keys));
	if (order->keys == NULL) {
		return false;
	}
	order->count = scan->count;
	order->text = read_index();
	if (order->text != NULL) {
		parse_index(order);
	}
	for (size_t i = 0; i < scan->count; i++) {
		order->keys[i] = stored_key(order, scan->paths[i]);
		order->unknown += order->keys[i] == MATUWALL_HUE_UNKNOWN;
	}
	if (!sort_scan(order, scan)) {
		return false;
	}
	matuwall_log_info("order", "colour order: %zu of %zu known",
		scan->count - order->unknown, scan->count);
	return true;
}

void matuwall_order_set(
	struct matuwall_order *order, size_t index, uint32_t key) {
	if (index < order->count && key != MATUWALL_HUE_UNKNOWN &&
		order->keys[index] != key) {
		order->keys[index] = key;
		order->dirty = true;
	}
}

// --- save ---

static bool write_entry(FILE *fp, uint32_t key, const char *path) {
	if (key == MATUWALL_HUE_UNKNOWN || strchr(path, '\n') != NULL) {
		return true;
	}
	return fprintf(fp, "%08x %s\n", key, path) > 0;
}

static bool write_entries(FILE *fp, const struct matuwall_order *order,
	const struct matuwall_dirscan *scan) {
	bool ok = fputs(HEADER, fp) >= 0;
	size_t written = 0;
	for (size_t i = 0; ok && i < order->count && i < scan->count; i++) {
		ok = write_entry(fp, order->keys[i], scan->paths[i]);
		written++;
	}
	// entries of other directories, or of files gone, fill the rest
	for (size_t i = 0; ok && i < order->entry_count; i++) {
		if (written == MAX_ENTRIES) {
			break;
		}
		if (!order->entries[i].used) {
			ok = write_entry(fp, order->entries[i].key,
				order->entries[i].path);
			written++;
		}
	}
	return ok;
}

static bool save_index(const struct matuwall_order *order,
	const struct matuwall_dirscan *scan) {
	char dir[PATH_MAX];
	char file[PATH_MAX];
	char tmp[PATH_MAX];
	if (!matuwall_fs_xdg_path(
		    "XDG_CACHE_HOME", "/.cache", "", dir, sizeof(dir)) ||
		!matuwall_fs_make_dirs(dir, 0700) ||
		!index_path(file, sizeof(file)) ||
		(size_t)snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", file) >=
			sizeof(tmp)) {
		return false;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		return false;
	}
	FILE *fp = fdopen(fd, "w");
	if (fp == NULL) {
		close(fd);
		unlink(tmp);
		return false;
	}
	bool ok = write_entries(fp, order, scan);
	// publish only a complete file
	if (fclose(fp) != 0 || !ok || rename(tmp, file) != 0) {
		unlink(tmp);
		return false;
	}
	return true;
}

void matuwall_order_finish(
	struct matuwall_order *order, const struct matuwall_dirscan *scan) {
	if (order->dirty && !save_index(order, scan)) {
		matuwall_log_warn("order", "cannot save the colour order");
	}
	free(order->keys);
	free(order->entries);
	free(order->text);
	*order = (struct matuwall_order){0};
}
