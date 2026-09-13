#ifndef MATUWALL_APP_INSTANCE_H
#define MATUWALL_APP_INSTANCE_H

#include <stdbool.h>

#include <limits.h>

struct matuwall_instance {
	int lock_fd;
	int socket_fd;
	char socket_path[PATH_MAX];
	bool owns_socket_path;
};

bool matuwall_instance_init(struct matuwall_instance *instance);

int matuwall_instance_fd(const struct matuwall_instance *instance);

bool matuwall_instance_dispatch(
	struct matuwall_instance *instance, bool *replace_requested);

void matuwall_instance_finish(struct matuwall_instance *instance);

#endif
