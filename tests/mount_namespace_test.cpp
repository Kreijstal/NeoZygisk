#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "empty_tmpfs.h"
#include "zygisk.hpp"

// Public process flag used by the real selector (module.hpp).
static constexpr uint32_t PROCESS_ROOT_IS_MAGISK = (1u << 30);

std::vector<mount_info> parse_mount_info(const char *pid);

static void directory(const char *path, mode_t mode = 0755) {
    assert(mkdir(path, mode) == 0);
}

static void file(const char *path, mode_t mode = 0600) {
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, mode);
    assert(fd >= 0);
    assert(close(fd) == 0);
}

static bool contains(const std::vector<mount_info>& entries, const char *path) {
    return std::any_of(entries.begin(), entries.end(), [path](const auto& info) {
        return info.target == path;
    });
}

int main(int argc, char **argv) {
    assert(argc == 2);
    // Invoked by unshare in fresh user/mount/PID namespaces. All test filesystem
    // contents live in a new tmpfs, and chroot confines the fixed policy paths.
    assert(mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0);
    assert(mount("tmpfs", argv[1], "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") == 0);
    const std::string proc = std::string(argv[1]) + "/proc";
    directory(proc.c_str());
    assert(mount("proc", proc.c_str(), "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) == 0);
    assert(chdir(argv[1]) == 0);
    assert(chroot(".") == 0);

    directory("/data"); directory("/data/adb");
    file(NZ_EMPTY_TMPFS_FLAG);
    directory("/mnt"); directory(NZ_EMPTY_TMPFS_TARGET, 0555);
    directory("/runtime"); directory("/other");
    assert(mount("tmpfs", "/runtime", "tmpfs", 0, "mode=755") == 0);
    assert(mount("tmpfs", "/other", "tmpfs", 0, "mode=755") == 0);
    directory("/runtime/.magisk", 0711);
    file("/runtime/.magisk/config");
    file("/runtime/magisk", 0755);
    directory("/runtime/.compat-empty", 0555);
    directory("/runtime/.magisk/worker");
    assert(mount("magisk", "/runtime/.magisk/worker", "tmpfs", 0, "mode=755") == 0);
    directory("/product"); directory("/product/bin");
    file("/product/bin/magisk", 0755);
    assert(mount("/runtime/magisk", "/product/bin/magisk", nullptr, MS_BIND, nullptr) == 0);
    assert(mount("/runtime/.compat-empty", NZ_EMPTY_TMPFS_TARGET, nullptr, MS_BIND, nullptr) == 0);
    assert(mount(nullptr, NZ_EMPTY_TMPFS_TARGET, nullptr, MS_PRIVATE, nullptr) == 0);
    assert(mount(nullptr, NZ_EMPTY_TMPFS_TARGET, nullptr,
                 MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) == 0);

    struct stat original, preserved;
    assert(stat("/runtime", &original) == 0);
    assert(stat(NZ_EMPTY_TMPFS_TARGET, &preserved) == 0);
    assert(original.st_dev == preserved.st_dev);
    assert(nz_magisk_tmpfs_anchor("/", "/runtime", "tmpfs", "tmpfs",
                                  major(original.st_dev), minor(original.st_dev)) == 1);
    assert(nz_magisk_tmpfs_anchor("/", "/other", "tmpfs", "tmpfs",
                                  major(original.st_dev), minor(original.st_dev)) == 0);
    int source_fd = open("/runtime/.compat-empty", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(source_fd >= 0);

    // This is the actual production C++ parser and cleanup selector.
    auto traces = check_zygote_traces(PROCESS_ROOT_IS_MAGISK);
    assert(contains(traces, "/runtime"));
    assert(contains(traces, "/product/bin/magisk"));
    assert(contains(traces, "/runtime/.magisk/worker"));
    assert(!contains(traces, NZ_EMPTY_TMPFS_TARGET));
    assert(!contains(traces, "/other"));

    assert(unlink(NZ_EMPTY_TMPFS_FLAG) == 0);
    assert(contains(check_zygote_traces(PROCESS_ROOT_IS_MAGISK), NZ_EMPTY_TMPFS_TARGET));
    file(NZ_EMPTY_TMPFS_FLAG);

    for (const auto& info : traces) assert(umount2(info.target.c_str(), MNT_DETACH) == 0);
    const auto after = parse_mount_info("self");
    assert(!contains(after, "/runtime"));
    assert(!contains(after, "/product/bin/magisk"));
    assert(!contains(after, "/runtime/.magisk/worker"));
    assert(contains(after, NZ_EMPTY_TMPFS_TARGET));
    assert(contains(after, "/other"));
    for (const auto& info : after) {
        assert(info.source != "magisk");
        if (info.target == NZ_EMPTY_TMPFS_TARGET) {
            assert(info.source == "tmpfs");
            assert(info.device == original.st_dev);
        }
    }
    assert(check_zygote_traces(PROCESS_ROOT_IS_MAGISK).empty());

    // A later nonempty view must be removed, even after the runtime root is gone.
    int injected = openat(source_fd, "unexpected-file", O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(injected >= 0);
    assert(close(injected) == 0);
    traces = check_zygote_traces(PROCESS_ROOT_IS_MAGISK);
    assert(traces.size() == 1 && contains(traces, NZ_EMPTY_TMPFS_TARGET));
    assert(umount2(NZ_EMPTY_TMPFS_TARGET, MNT_DETACH) == 0);
    assert(close(source_fd) == 0);
    std::printf("PASS: Linux mount integration; retained %u:%u with source tmpfs; "
                "removed runtime/binary/worker mounts; rejected nonempty view\n",
                major(original.st_dev), minor(original.st_dev));
}
