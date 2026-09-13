#ifndef MATUWALL_UTIL_LOG_H
#define MATUWALL_UTIL_LOG_H

#include <stdbool.h>
#include <stddef.h>

bool matuwall_log_path(char *out, size_t out_size);

void matuwall_log_start(const char *version);
bool matuwall_log_activate(void);
void matuwall_log_finish(void);

void matuwall_log_info(const char *component, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
void matuwall_log_warn(const char *component, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
void matuwall_log_error(const char *component, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

#endif
