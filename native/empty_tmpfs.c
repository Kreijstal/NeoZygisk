#include "empty_tmpfs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <unistd.h>

#define NZ_TMPFS_MAGIC 0x01021994

int nz_magisk_tmpfs_anchor(const char *root, const char *target, const char *type,
                          const char *source, uint32_t device_major, uint32_t device_minor) {
    if (!root || !target || !type || !source || device_major != 0 || device_minor == 0 ||
        strcmp(type, "tmpfs") || strcmp(source, "tmpfs")) return 0;

    // These are superblock-relative paths from kernel mountinfo, not basenames
    // matched against every mount target. They also work after the runtime root
    // has been detached. An invalid empty view is identified for removal here;
    // only nz_keep_empty_tmpfs may grant the exception.
    if (!strcmp(root, "/magisk") || !strcmp(root, "/magiskpolicy") ||
        (!strcmp(root, NZ_EMPTY_TMPFS_ROOT) && !strcmp(target, NZ_EMPTY_TMPFS_TARGET))) return 1;

    // Support custom runtime locations without hardcoding /sbin or /debug_ramdisk.
    const int fd = open(target, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1) return 0;
    struct stat config;
    const int found = fstatat(fd, ".magisk/config", &config, AT_SYMLINK_NOFOLLOW) == 0 &&
                      S_ISREG(config.st_mode) && config.st_uid == 0 &&
                      major(config.st_dev) == device_major && minor(config.st_dev) == device_minor;
    close(fd);
    return found;
}

static int has_option(const char *options, const char *wanted) {
    const size_t wanted_len = strlen(wanted);
    // C++'s mountinfo reader includes optional propagation fields after a space.
    while (*options && *options != ' ') {
        const size_t len = strcspn(options, ", ");
        if (len == wanted_len && memcmp(options, wanted, len) == 0) return 1;
        options += len;
        if (*options != ',') break;
        ++options;
    }
    return 0;
}

static int enabled(void) {
    const int fd = open(NZ_EMPTY_TMPFS_FLAG, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd == -1) return 0;
    struct stat st;
    const int valid = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
                      st.st_uid == 0 && st.st_gid == 0 &&
                      (st.st_mode & 07777) == 0600 && st.st_size == 0 && st.st_nlink == 1;
    close(fd);
    return valid;
}

int nz_keep_empty_tmpfs(const char *root, const char *target, const char *type,
                        const char *source, const char *vfs_options,
                        uint32_t device_major, uint32_t device_minor) {
    if (!root || !target || !type || !source || !vfs_options ||
        strcmp(root, NZ_EMPTY_TMPFS_ROOT) || strcmp(target, NZ_EMPTY_TMPFS_TARGET) ||
        strcmp(type, "tmpfs") || (strcmp(source, "magisk") && strcmp(source, "tmpfs")) ||
        device_major != 0 || device_minor == 0 ||
        !has_option(vfs_options, "ro") || has_option(vfs_options, "rw") ||
        !has_option(vfs_options, "nosuid") || !has_option(vfs_options, "nodev") ||
        !has_option(vfs_options, "noexec") || !enabled()) return 0;

    // Validate the live mount, not just text supplied by the mountinfo parser.
    // A symlink, writable directory, nonempty view or a different filesystem
    // must never become an exception to ordinary root/module cleanup.
    const int fd = open(target, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1) return 0;
    struct stat st;
    struct statfs fs;
    if (fstat(fd, &st) || fstatfs(fd, &fs) || !S_ISDIR(st.st_mode) ||
        st.st_uid != 0 || st.st_gid != 0 || (st.st_mode & 07777) != 0555 ||
        major(st.st_dev) != device_major || minor(st.st_dev) != device_minor ||
        fs.f_type != NZ_TMPFS_MAGIC || !(fs.f_flags & ST_RDONLY) ||
        !(fs.f_flags & ST_NOSUID) || !(fs.f_flags & ST_NODEV) ||
        !(fs.f_flags & ST_NOEXEC)) {
        close(fd);
        return 0;
    }
    DIR *dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return 0;
    }
    int empty = 1;
    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            empty = 0;
            break;
        }
    }
    if (errno) empty = 0;
    closedir(dir);
    return empty;
}
