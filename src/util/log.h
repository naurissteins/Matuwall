#ifndef SWEETWALL_UTIL_LOG_H
#define SWEETWALL_UTIL_LOG_H

#include <stdbool.h>

void sweetwall_log_start(const char *version);
bool sweetwall_log_activate(void);
void sweetwall_log_finish(void);

void sweetwall_log_info(const char *component, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
void sweetwall_log_warn(const char *component, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
void sweetwall_log_error(const char *component, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

#endif
