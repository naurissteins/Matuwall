#ifndef MATUWALL_UTIL_FS_H
#define MATUWALL_UTIL_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

// $XDG_STATE_HOME/matuwall, or ~/.local/state/matuwall
bool matuwall_fs_state_dir(char *out, size_t out_size);

// mkdir -p, existing directories keep their mode
bool matuwall_fs_make_dirs(const char *path, mode_t mode);

// both retry EINTR and short transfers, false on error or EOF
bool matuwall_fs_read_all(int fd, void *data, size_t size);

bool matuwall_fs_write_all(int fd, const void *data, size_t size);

#endif
