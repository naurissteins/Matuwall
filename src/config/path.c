#include "config/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void normalize(char *path) {
	size_t w = 1;
	size_t r = 1;
	while (path[r] != '\0') {
		while (path[r] == '/') {
			r++;
		}
		size_t len = strcspn(path + r, "/");
		if (len == 0) {
			break;
		}
		if (len == 1 && path[r] == '.') {
			r += len;
			continue;
		}
		if (len == 2 && path[r] == '.' && path[r + 1] == '.') {
			while (w > 1 && path[w - 1] != '/') {
				w--;
			}
			if (w > 1) {
				w--;
			}
			r += len;
			continue;
		}
		if (w > 1) {
			path[w++] = '/';
		}
		memmove(path + w, path + r, len);
		w += len;
		r += len;
	}
	path[w] = '\0';
}

bool matuwall_config_expand_path(const char *in, char *out, size_t out_size) {
	int n;
	if (in[0] == '\0') {
		return false;
	}
	if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
		const char *home = getenv("HOME");
		if (home == NULL) {
			return false;
		}
		n = snprintf(out, out_size, "%s%s", home, in + 1);
	} else if (in[0] != '/') {
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			return false;
		}
		n = snprintf(out, out_size, "%s/%s", cwd, in);
	} else {
		n = snprintf(out, out_size, "%s", in);
	}
	if (n <= 0 || (size_t)n >= out_size) {
		return false;
	}
	if (out[0] == '/') {
		normalize(out);
	}
	return true;
}

bool matuwall_config_add_directory(
	struct matuwall_directories *dirs, const char *path) {
	char expanded[PATH_MAX];
	if (!matuwall_config_expand_path(path, expanded, sizeof(expanded))) {
		return false;
	}
	for (size_t i = 0; i < dirs->count; i++) {
		if (strcmp(dirs->items[i], expanded) == 0) {
			return true;
		}
	}
	if (dirs->count == MATUWALL_MAX_DIRECTORIES) {
		return false;
	}
	memcpy(dirs->items[dirs->count++], expanded, strlen(expanded) + 1);
	return true;
}

const char *matuwall_config_directory(const struct matuwall_config *cfg) {
	return cfg->directories.count > 0 ? cfg->directories.items[0] : "";
}

const char *matuwall_config_source(
	const struct matuwall_config *cfg, char *buf, size_t size) {
	if (cfg->list[0] != '\0') {
		return cfg->list;
	}
	if (cfg->directories.count == 1) {
		return cfg->directories.items[0];
	}
	snprintf(buf, size, "%zu directories", cfg->directories.count);
	return buf;
}
