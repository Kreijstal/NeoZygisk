#include "empty_tmpfs.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>

// These wrappers model kernel responses, not the validation policy itself.
static struct stat flag_stat, view_stat;
static struct statfs view_fs;
static struct stat config_stat;
static int config_error;
static const char *anchor_target;
static int flag_missing, view_missing, stat_error, fs_error, dir_error, read_error;
static int nonempty, next_entry, open_fds, tests;

int __wrap_open(const char *path, int flags, ...) {
    assert(flags & O_NOFOLLOW);
    assert(flags & O_CLOEXEC);
    if (!strcmp(path, NZ_EMPTY_TMPFS_FLAG)) {
        assert(flags & O_NONBLOCK);
        if (flag_missing) return -1;
        ++open_fds;
        return 10;
    }
    assert(!strcmp(path, anchor_target ? anchor_target : NZ_EMPTY_TMPFS_TARGET));
    assert(flags & O_DIRECTORY);
    if (view_missing) return -1;
    ++open_fds;
    return 11;
}

int __wrap_close(int fd) {
    assert(fd == 10 || fd == 11);
    --open_fds;
    return 0;
}

int __wrap_fstat(int fd, struct stat *st) {
    assert(fd == 10 || fd == 11);
    if (stat_error == fd) return -1;
    *st = fd == 10 ? flag_stat : view_stat;
    return 0;
}

int __wrap_fstatfs(int fd, struct statfs *fs) {
    assert(fd == 11);
    if (fs_error) return -1;
    *fs = view_fs;
    return 0;
}

int __wrap_fstatat(int fd, const char *path, struct stat *st, int flags) {
    assert(fd == 11);
    assert(!strcmp(path, ".magisk/config"));
    assert(flags == AT_SYMLINK_NOFOLLOW);
    if (config_error) return -1;
    *st = config_stat;
    return 0;
}

DIR *__wrap_fdopendir(int fd) {
    assert(fd == 11);
    if (dir_error) return NULL;
    next_entry = 0;
    return (DIR *)1;
}

struct dirent *__wrap_readdir(DIR *dir) {
    assert(dir == (DIR *)1);
    static struct dirent entry;
    const char *names[] = {".", "..", "magisk"};
    if (next_entry >= (nonempty ? 3 : 2)) {
        if (read_error) errno = EIO;
        return NULL;
    }
    strcpy(entry.d_name, names[next_entry++]);
    return &entry;
}

int __wrap_closedir(DIR *dir) {
    assert(dir == (DIR *)1);
    --open_fds;
    return 0;
}

static void reset(void) {
    assert(open_fds == 0);
    flag_stat = (struct stat){.st_mode = S_IFREG | 0600, .st_nlink = 1};
    view_stat = (struct stat){.st_mode = S_IFDIR | 0555, .st_dev = makedev(0, 21)};
    config_stat = (struct stat){.st_mode = S_IFREG | 0600, .st_dev = makedev(0, 21)};
    config_error = 0;
    anchor_target = NULL;
    view_fs = (struct statfs){.f_type = 0x01021994,
                             .f_flags = ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC};
    flag_missing = view_missing = stat_error = fs_error = dir_error = read_error = nonempty = 0;
}

static void expect(int result, const char *root, const char *target, const char *type,
                   const char *source, const char *options, unsigned int maj, unsigned int min) {
    assert(nz_keep_empty_tmpfs(root, target, type, source, options, maj, min) == result);
    assert(open_fds == 0);
    ++tests;
}

#define CHECK(result) expect(result, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", \
                             "magisk", "ro,nosuid,nodev,noexec", 0, 21)
#define REJECT(change) do { reset(); change; CHECK(0); } while (0)

int main(void) {
    reset(); CHECK(1);
    REJECT(flag_missing = 1);
    REJECT(flag_stat.st_mode = S_IFLNK | 0600);
    REJECT(flag_stat.st_mode = S_IFREG | 0644);
    REJECT(flag_stat.st_uid = 1000);
    REJECT(flag_stat.st_gid = 1000);
    REJECT(flag_stat.st_size = 1);
    REJECT(flag_stat.st_nlink = 2);
    REJECT(stat_error = 10);
    REJECT(view_missing = 1); // Includes open(O_NOFOLLOW) rejecting a symlink.
    REJECT(stat_error = 11);
    REJECT(fs_error = 1);
    REJECT(dir_error = 1);
    REJECT(read_error = 1);
    REJECT(nonempty = 1);
    REJECT(view_stat.st_mode = S_IFDIR | 0755);
    REJECT(view_stat.st_mode = S_IFREG | 0555);
    REJECT(view_stat.st_uid = 1000);
    REJECT(view_stat.st_gid = 1000);
    REJECT(view_stat.st_dev = makedev(0, 22));
    REJECT(view_fs.f_type = 0xef53);
    REJECT(view_fs.f_flags &= ~ST_RDONLY);
    REJECT(view_fs.f_flags &= ~ST_NOSUID);
    REJECT(view_fs.f_flags &= ~ST_NODEV);
    REJECT(view_fs.f_flags &= ~ST_NOEXEC);
    reset();
    expect(0, "/", NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 0, 21);
    expect(0, "/magisk", NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, "/product/bin/magisk", "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "overlay", "magisk", "ro,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "other", "ro,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "rw,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,rw,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexecx", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev shared:noexec", 0, 21);
    expect(1, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec shared:9", 0, 21);
    expect(1, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "tmpfs", "ro,nosuid,nodev,noexec", 0, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 1, 21);
    expect(0, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 0, 0);
    expect(0, NULL, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 0, 21);
    view_stat.st_dev = makedev(0, 99);
    expect(1, NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "magisk", "ro,nosuid,nodev,noexec", 0, 99);

    reset();
    anchor_target = "/custom-runtime";
    assert(nz_magisk_tmpfs_anchor("/", anchor_target, "tmpfs", "tmpfs", 0, 21)); ++tests;
    config_stat.st_dev = makedev(0, 22);
    assert(!nz_magisk_tmpfs_anchor("/", anchor_target, "tmpfs", "tmpfs", 0, 21)); ++tests;
    config_stat.st_dev = makedev(0, 21);
    config_stat.st_uid = 1000;
    assert(!nz_magisk_tmpfs_anchor("/", anchor_target, "tmpfs", "tmpfs", 0, 21)); ++tests;
    config_stat.st_uid = 0;
    config_stat.st_mode = S_IFLNK | 0600;
    assert(!nz_magisk_tmpfs_anchor("/", anchor_target, "tmpfs", "tmpfs", 0, 21)); ++tests;
    config_error = 1;
    assert(!nz_magisk_tmpfs_anchor("/", anchor_target, "tmpfs", "tmpfs", 0, 21)); ++tests;
    assert(nz_magisk_tmpfs_anchor("/magisk", "/product/bin/magisk", "tmpfs", "tmpfs", 0, 21)); ++tests;
    assert(nz_magisk_tmpfs_anchor("/magiskpolicy", "/product/bin/magiskpolicy", "tmpfs", "tmpfs", 0, 21)); ++tests;
    assert(nz_magisk_tmpfs_anchor(NZ_EMPTY_TMPFS_ROOT, NZ_EMPTY_TMPFS_TARGET, "tmpfs", "tmpfs", 0, 21)); ++tests;
    assert(!nz_magisk_tmpfs_anchor("/magisk", "/product/bin/magisk", "ext4", "tmpfs", 0, 21)); ++tests;
    assert(!nz_magisk_tmpfs_anchor("/magisk", "/product/bin/magisk", "tmpfs", "tmpfs", 1, 21)); ++tests;
    assert(!nz_magisk_tmpfs_anchor("/magisk", "/product/bin/magisk", "tmpfs", "other", 0, 21)); ++tests;
    assert(open_fds == 0);
    printf("PASS: %d empty-tmpfs policy cases, no leaked descriptors\n", tests);
}
