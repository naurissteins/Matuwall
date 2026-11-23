#include "state/selection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/fs.h"

#define STATE_FILE "last-selection"
#define OUTPUTS_FILE "last-selection-outputs"

static bool state_file(const char *leaf, char *path, size_t path_size) {
	char dir[PATH_MAX];
	if (!matuwall_fs_state_dir(dir, sizeof(dir))) {
		return false;
	}
	int length = snprintf(path, path_size, "%s/%s", dir, leaf);
	return length > 0 && (size_t)length < path_size;
}

bool matuwall_selection_path(char *path, size_t path_size) {
	return state_file(STATE_FILE, path, path_size);
}

// a regular file of at most size bytes, false when missing or larger
static bool read_state(
	const char *leaf, char *data, size_t size, size_t *length) {
	char file[PATH_MAX];
	if (!state_file(leaf, file, sizeof(file))) {
		return false;
	}
	int fd = open(file, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		return false;
	}
	struct stat info;
	ssize_t n = -1;
	if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode)) {
		n = read(fd, data, size);
	}
	bool ok = close(fd) == 0 && n > 0 && (size_t)n < size;
	*length = ok ? (size_t)n : 0;
	return ok;
}

static bool write_state(const char *leaf, const char *data, size_t length) {
	char dir[PATH_MAX];
	char file[PATH_MAX];
	char tmp[PATH_MAX];
	if (!matuwall_fs_state_dir(dir, sizeof(dir)) ||
		!matuwall_fs_make_dirs(dir, 0700) ||
		!state_file(leaf, file, sizeof(file)) ||
		(size_t)snprintf(tmp, sizeof(tmp), "%s.tmpXXXXXX", file) >=
			sizeof(tmp)) {
		return false;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		return false;
	}
	bool ok = matuwall_fs_write_all(fd, data, length);
	// publish only a complete file
	if (close(fd) != 0 || !ok || rename(tmp, file) != 0) {
		unlink(tmp);
		return false;
	}
	return true;
}

// file holds the raw path bytes, nothing else
bool matuwall_selection_load(char *path, size_t path_size) {
	size_t length;
	if (path_size == 0) {
		return false;
	}
	bool ok = read_state(STATE_FILE, path, path_size, &length) &&
		  path[0] == '/' && memchr(path, '\0', length) == NULL;
	path[ok ? length : 0] = '\0';
	return ok;
}

#define OUTPUTS_BYTES                                                          \
	(MATUWALL_SELECTION_OUTPUTS * (MATUWALL_SELECTION_NAME_MAX + PATH_MAX))

// copy one NUL-terminated field of at most size - 1 bytes
static bool take_field(
	const char **p, const char *end, char *out, size_t size) {
	const char *nul = memchr(*p, '\0', (size_t)(end - *p));
	size_t len = nul != NULL ? (size_t)(nul - *p) : size;
	if (len == 0 || len >= size) {
		return false;
	}
	memcpy(out, *p, len + 1);
	*p = nul + 1;
	return true;
}

void matuwall_selection_load_outputs(struct matuwall_selection_outputs *out) {
	out->count = 0;
	char *data = malloc(OUTPUTS_BYTES + 1);
	size_t length;
	if (data == NULL ||
		!read_state(OUTPUTS_FILE, data, OUTPUTS_BYTES + 1, &length)) {
		free(data);
		return;
	}
	const char *p = data;
	const char *end = data + length;
	while (p < end && out->count < MATUWALL_SELECTION_OUTPUTS) {
		size_t i = out->count;
		if (!take_field(&p, end, out->items[i].name,
			    sizeof(out->items[i].name)) ||
			!take_field(&p, end, out->items[i].path,
				sizeof(out->items[i].path)) ||
			out->items[i].path[0] != '/') {
			out->count = 0; // a corrupt file is no state at all
			break;
		}
		out->count++;
	}
	free(data);
}

bool matuwall_selection_load_for(
	const char *output, char *path, size_t path_size) {
	if (output != NULL && path_size > 0) {
		struct matuwall_selection_outputs *outputs =
			malloc(sizeof(*outputs));
		if (outputs != NULL) {
			matuwall_selection_load_outputs(outputs);
			for (size_t i = 0; i < outputs->count; i++) {
				size_t len = strlen(outputs->items[i].path);
				if (strcmp(outputs->items[i].name, output) ==
						0 &&
					len < path_size) {
					memcpy(path, outputs->items[i].path,
						len + 1);
					free(outputs);
					return true;
				}
			}
			free(outputs);
		}
	}
	return matuwall_selection_load(path, path_size);
}

static size_t append_pair(
	char *data, size_t length, const char *name, const char *path) {
	size_t name_len = strlen(name) + 1;
	size_t path_len = strlen(path) + 1;
	memcpy(data + length, name, name_len);
	memcpy(data + length + name_len, path, path_len);
	return length + name_len + path_len;
}

// output's entry moves to the end, the oldest falls off when full
static bool save_output(const char *path, const char *output) {
	if (strlen(output) >= MATUWALL_SELECTION_NAME_MAX ||
		output[0] == '\0' || strlen(path) >= PATH_MAX) {
		return false;
	}
	struct {
		struct matuwall_selection_outputs outputs;
		char data[OUTPUTS_BYTES];
	} *scratch = malloc(sizeof(*scratch));
	if (scratch == NULL) {
		return false;
	}
	struct matuwall_selection_outputs *outputs = &scratch->outputs;
	matuwall_selection_load_outputs(outputs);
	bool known = false;
	for (size_t i = 0; i < outputs->count; i++) {
		known |= strcmp(outputs->items[i].name, output) == 0;
	}
	size_t length = 0;
	size_t start =
		!known && outputs->count == MATUWALL_SELECTION_OUTPUTS ? 1 : 0;
	for (size_t i = start; i < outputs->count; i++) {
		if (strcmp(outputs->items[i].name, output) != 0) {
			length = append_pair(scratch->data, length,
				outputs->items[i].name, outputs->items[i].path);
		}
	}
	length = append_pair(scratch->data, length, output, path);
	bool ok = write_state(OUTPUTS_FILE, scratch->data, length);
	free(scratch);
	return ok;
}

bool matuwall_selection_has_outputs(void) {
	char file[PATH_MAX];
	return state_file(OUTPUTS_FILE, file, sizeof(file)) &&
	       access(file, F_OK) == 0;
}

static bool drop_outputs(void) {
	char file[PATH_MAX];
	return state_file(OUTPUTS_FILE, file, sizeof(file)) &&
	       (unlink(file) == 0 || errno == ENOENT);
}

bool matuwall_selection_save(const char *path, const char *output) {
	bool ok = write_state(STATE_FILE, path, strlen(path));
	return (output != NULL ? save_output(path, output) : drop_outputs()) &&
	       ok;
}
