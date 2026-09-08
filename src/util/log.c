#include "util/log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_FILE "sweetwall.log"
#define LOG_FILE_1 "sweetwall.log.1"
#define LOG_FILE_2 "sweetwall.log.2"
#define LOG_MAX_BYTES ((size_t)256 * 1024)
#define LOG_LINE_MAX 1024u
#define LOG_EARLY_MAX 32u
#define LOG_COMPONENT_MAX 24u
#define LOG_MESSAGE_MAX 448u

enum log_level {
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR,
};

struct early_record {
	struct timespec at;
	enum log_level level;
	char component[LOG_COMPONENT_MAX];
	char message[LOG_MESSAGE_MAX];
};

struct logger_state {
	int dir_fd;
	int file_fd;
	size_t file_size;
	struct early_record early[LOG_EARLY_MAX];
	size_t early_count;
	bool started;
	bool dropped;
};

static struct logger_state logger = {
	.dir_fd = -1,
	.file_fd = -1,
};

static const char *level_name(enum log_level level) {
	switch (level) {
	case LOG_WARN:
		return "WARN ";
	case LOG_ERROR:
		return "ERROR";
	case LOG_INFO:
		break;
	}
	return "INFO ";
}

static bool state_dir(char *out, size_t out_size) {
	const char *xdg = getenv("XDG_STATE_HOME");
	if (xdg != NULL && xdg[0] == '/') {
		return (size_t)snprintf(out, out_size, "%s/sweetwall", xdg) <
		       out_size;
	}
	const char *home = getenv("HOME");
	if (home == NULL) {
		return false;
	}
	return (size_t)snprintf(out, out_size, "%s/.local/state/sweetwall",
		       home) < out_size;
}

bool sweetwall_log_path(char *out, size_t out_size) {
	char dir[PATH_MAX];
	if (!state_dir(dir, sizeof(dir))) {
		return false;
	}
	int length = snprintf(out, out_size, "%s/%s", dir, LOG_FILE);
	return length > 0 && (size_t)length < out_size;
}

static bool make_dir(const char *path) {
	char copy[PATH_MAX];
	size_t length = strlen(path);
	if (length >= sizeof(copy)) {
		return false;
	}
	memcpy(copy, path, length + 1);

	for (char *p = copy + 1; *p != '\0'; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
			return false;
		}
		*p = '/';
	}
	return mkdir(copy, 0700) == 0 || errno == EEXIST;
}

static bool write_all(int fd, const char *data, size_t size) {
	while (size > 0) {
		ssize_t count = write(fd, data, size);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return false;
		}
		data += (size_t)count;
		size -= (size_t)count;
	}
	return true;
}

static bool open_file(void) {
	logger.file_fd = openat(logger.dir_fd, LOG_FILE,
		O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (logger.file_fd < 0) {
		return false;
	}
	struct stat info;
	if (fstat(logger.file_fd, &info) != 0 || !S_ISREG(info.st_mode) ||
		info.st_size < 0 || fchmod(logger.file_fd, 0600) != 0) {
		close(logger.file_fd);
		logger.file_fd = -1;
		return false;
	}
	logger.file_size = (size_t)info.st_size;
	return true;
}

static bool rotate(void) {
	if (logger.file_fd >= 0) {
		close(logger.file_fd);
		logger.file_fd = -1;
	}
	if (unlinkat(logger.dir_fd, LOG_FILE_2, 0) != 0 && errno != ENOENT) {
		return false;
	}
	if (renameat(logger.dir_fd, LOG_FILE_1, logger.dir_fd, LOG_FILE_2) !=
			0 &&
		errno != ENOENT) {
		return false;
	}
	if (renameat(logger.dir_fd, LOG_FILE, logger.dir_fd, LOG_FILE_1) != 0 &&
		errno != ENOENT) {
		return false;
	}
	return open_file();
}

static bool persist(const char *line, size_t size) {
	if (logger.file_fd < 0) {
		return false;
	}
	if (logger.file_size > LOG_MAX_BYTES - size && !rotate()) {
		return false;
	}
	if (!write_all(logger.file_fd, line, size)) {
		return false;
	}
	logger.file_size += size;
	return true;
}

static void close_log(void) {
	if (logger.file_fd >= 0) {
		close(logger.file_fd);
		logger.file_fd = -1;
	}
	if (logger.dir_fd >= 0) {
		close(logger.dir_fd);
		logger.dir_fd = -1;
	}
}

static size_t format_line(char *out, size_t out_size, const struct timespec *at,
	enum log_level level, const char *component, const char *message) {
	struct tm local;
	if (localtime_r(&at->tv_sec, &local) == NULL) {
		memset(&local, 0, sizeof(local));
	}

	int prefix = snprintf(out, out_size,
		"%04d-%02d-%02d %02d:%02d:%02d.%03ld %s %s: ",
		local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
		local.tm_hour, local.tm_min, local.tm_sec,
		at->tv_nsec / 1000000, level_name(level), component);
	if (prefix < 0 || (size_t)prefix >= out_size) {
		return 0;
	}

	int written = snprintf(
		out + prefix, out_size - (size_t)prefix, "%s", message);
	if (written < 0) {
		return 0;
	}
	size_t length = (size_t)prefix + (size_t)written;
	if (length >= out_size - 1) {
		length = out_size - 2;
	}
	for (size_t i = (size_t)prefix; i < length; i++) {
		if ((unsigned char)out[i] < 0x20) {
			out[i] = ' ';
		}
	}
	out[length++] = '\n';
	out[length] = '\0';
	return length;
}

static void buffer_message(const struct timespec *at, enum log_level level,
	const char *component, const char *message) {
	if (logger.early_count == LOG_EARLY_MAX) {
		logger.dropped = true;
		return;
	}
	struct early_record *record = &logger.early[logger.early_count++];
	record->at = *at;
	record->level = level;
	snprintf(record->component, sizeof(record->component), "%s", component);
	snprintf(record->message, sizeof(record->message), "%s", message);
}

static void log_message(enum log_level level, const char *component,
	const char *format, va_list args) {
	char message[LOG_MESSAGE_MAX];
	int written = vsnprintf(message, sizeof(message), format, args);
	if (written < 0) {
		return;
	}
	size_t message_size = (size_t)written;
	if (message_size >= sizeof(message)) {
		message_size = sizeof(message) - 1;
	}
	for (size_t i = 0; i < message_size; i++) {
		if ((unsigned char)message[i] < 0x20) {
			message[i] = ' ';
		}
	}
	message[message_size] = '\0';

	if (level != LOG_INFO) {
		const char *label = level == LOG_ERROR ? "error" : "warning";
		fprintf(stderr, "sweetwall: %s: %s: %s\n", label, component,
			message);
	}
	struct timespec at = {0};
	clock_gettime(CLOCK_REALTIME, &at);
	if (logger.file_fd >= 0) {
		char line[LOG_LINE_MAX];
		size_t size = format_line(
			line, sizeof(line), &at, level, component, message);
		if (size == 0) {
			return;
		}
		if (!persist(line, size)) {
			int saved = errno;
			fprintf(stderr,
				"sweetwall: warning: logging: cannot write "
				"persistent log: %s\n",
				strerror(saved));
			close_log();
			buffer_message(&at, level, component, message);
		}
	} else if (logger.started) {
		buffer_message(&at, level, component, message);
	}
}

void sweetwall_log_start(const char *version) {
	logger.started = true;
	sweetwall_log_info(
		"startup", "sweetwall %s (pid %ld)", version, (long)getpid());
}

bool sweetwall_log_activate(void) {
	if (logger.file_fd >= 0) {
		return true;
	}
	char path[PATH_MAX];
	if (!state_dir(path, sizeof(path)) || !make_dir(path)) {
		return false;
	}
	logger.dir_fd =
		open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (logger.dir_fd < 0 || fchmod(logger.dir_fd, 0700) != 0 ||
		!open_file()) {
		if (logger.dir_fd >= 0) {
			close(logger.dir_fd);
			logger.dir_fd = -1;
		}
		return false;
	}
	if (logger.file_size >= LOG_MAX_BYTES && !rotate()) {
		close_log();
		return false;
	}
	for (size_t i = 0; i < logger.early_count; i++) {
		const struct early_record *record = &logger.early[i];
		char line[LOG_LINE_MAX];
		size_t size = format_line(line, sizeof(line), &record->at,
			record->level, record->component, record->message);
		if (size == 0 || !persist(line, size)) {
			close_log();
			return false;
		}
	}
	logger.early_count = 0;
	if (logger.dropped) {
		logger.dropped = false;
		sweetwall_log_warn("logging", "early log buffer overflowed");
	}
	return true;
}

void sweetwall_log_finish(void) {
	if (logger.started && logger.file_fd < 0) {
		sweetwall_log_activate();
	}
	close_log();
	logger = (struct logger_state){
		.dir_fd = -1,
		.file_fd = -1,
	};
}

void sweetwall_log_info(const char *component, const char *format, ...) {
	va_list args;
	va_start(args, format);
	log_message(LOG_INFO, component, format, args);
	va_end(args);
}

void sweetwall_log_warn(const char *component, const char *format, ...) {
	va_list args;
	va_start(args, format);
	log_message(LOG_WARN, component, format, args);
	va_end(args);
}

void sweetwall_log_error(const char *component, const char *format, ...) {
	va_list args;
	va_start(args, format);
	log_message(LOG_ERROR, component, format, args);
	va_end(args);
}
