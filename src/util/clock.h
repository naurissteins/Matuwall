#ifndef MATUWALL_UTIL_CLOCK_H
#define MATUWALL_UTIL_CLOCK_H

#include <stdint.h>
#include <time.h>

// Monotonic milliseconds, shared by key repeat, the spinner, and preview dwell
static inline int64_t matuwall_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif
