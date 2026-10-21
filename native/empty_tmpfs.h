#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// An opt-in exception for one prepared bind mount, not a general mount allowlist.
#define NZ_EMPTY_TMPFS_FLAG "/data/adb/neozygisk-preserve-empty-tmpfs"
#define NZ_EMPTY_TMPFS_ROOT "/.compat-empty"
#define NZ_EMPTY_TMPFS_TARGET "/mnt/compat-empty"

int nz_keep_empty_tmpfs(const char *root, const char *target, const char *type,
                        const char *source, const char *vfs_options,
                        uint32_t device_major, uint32_t device_minor);

// Identify a Magisk runtime filesystem even when it uses the normal tmpfs label.
// Callers collect its device number before removing any mounts, then clean all
// tmpfs binds on that filesystem except the independently validated empty view.
int nz_magisk_tmpfs_anchor(const char *root, const char *target, const char *type,
                          const char *source, uint32_t device_major, uint32_t device_minor);

#ifdef __cplusplus
}
#endif
