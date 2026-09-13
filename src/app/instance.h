#ifndef SWEETWALL_APP_INSTANCE_H
#define SWEETWALL_APP_INSTANCE_H

#include <stdbool.h>

#include <limits.h>

struct sweetwall_instance {
	int lock_fd;
	int socket_fd;
	char socket_path[PATH_MAX];
	bool owns_socket_path;
};

bool sweetwall_instance_init(struct sweetwall_instance *instance);

int sweetwall_instance_fd(const struct sweetwall_instance *instance);

bool sweetwall_instance_dispatch(
	struct sweetwall_instance *instance, bool *replace_requested);

void sweetwall_instance_finish(struct sweetwall_instance *instance);

#endif
