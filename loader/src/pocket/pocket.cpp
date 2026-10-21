// SPDX-License-Identifier: GPL-3.0-only
// Cooperative Pocket backend. No ptrace, setns, mount, or map concealment.
#include "pocket.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "module.hpp"

ZygiskContext *g_ctx = nullptr;
HookContext *g_hook = nullptr;
int (*old_fork)() = nullptr;

namespace {
struct ArtCallbacks {
    const char *(*shorty)(JNIEnv *, jmethodID);
    uint32_t (*count)(JNIEnv *, jclass);
    uint32_t (*methods)(JNIEnv *, jclass, JNINativeMethod *, uint32_t);
} callbacks{};
std::vector<JNINativeMethod> originals;
std::vector<std::string> module_dirs;
constexpr const char *zygote_class = "com/android/internal/os/Zygote";
int cooperative_fork() { return (g_ctx && g_ctx->pid >= 0) ? g_ctx->pid : old_fork(); }
std::vector<JNINativeMethod> natives(JNIEnv *env, jclass clazz) {
    uint32_t count = callbacks.count(env, clazz);
    if (count > 65536) return {};
    std::vector<JNINativeMethod> methods(count);
    callbacks.methods(env, clazz, methods.data(), count);
    return methods;
}
}  // namespace

namespace pocket {
void unsupported(const char *feature) {
    fprintf(stderr, "NEO_POCKET_UNSUPPORTED %s\n", feature);
    errno = ENOTSUP;
}
std::string module_path(size_t id) {
    return id < module_dirs.size() ? module_dirs[id] + "/zygisk/arm64-v8a.so" : "";
}
}  // namespace pocket

// Reuse NeoZygisk's module ABI and lifecycle; substitute only its privileged
// transport with a process-local scan of the guest administrator's modules.
namespace zygiskd {
std::vector<Module> ReadModules() {
    module_dirs.clear();
    std::vector<Module> result;
    DIR *dir = opendir("/data/adb/modules");
    if (!dir) return result;
    std::vector<std::string> names;
    while (dirent *entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' ||
            name.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
                std::string::npos)
            continue;
        names.push_back(name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    for (const auto &name : names) {
        std::string base = "/data/adb/modules/" + name;
        struct stat st{};
        if (lstat(base.c_str(), &st) || !S_ISDIR(st.st_mode) ||
            !access((base + "/disable").c_str(), F_OK) ||
            !access((base + "/remove").c_str(), F_OK) ||
            access((base + "/module.prop").c_str(), R_OK))
            continue;
        int fd = open((base + "/zygisk/arm64-v8a.so").c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        module_dirs.push_back(base);
        result.emplace_back(name, fd);
    }
    return result;
}
uint32_t GetProcessFlags(uid_t) { return 0; }  // No claim of host root or denylist isolation.
int GetModuleDir(size_t id) {
    if (id >= module_dirs.size()) {
        errno = EINVAL;
        return -1;
    }
    return open(module_dirs[id].c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}
int ConnectCompanion(size_t) {
    pocket::unsupported("connectCompanion");
    return -1;
}
void SystemServerStarted() {}
}  // namespace zygiskd

ZygiskContext::ZygiskContext(JNIEnv *env, void *args)
    : env(env),
      args{args},
      process(nullptr),
      pid(-1),
      flags(0),
      info_flags(0),
      allowed_fds([] {
          rlimit r{32768, 32768};
          getrlimit(RLIMIT_NOFILE, &r);
          return std::min<rlim_t>(r.rlim_max, 1048576);
      }()),
      hook_info_lock(PTHREAD_MUTEX_INITIALIZER) {
    g_ctx = this;
}
ZygiskContext::~ZygiskContext() {
    g_ctx = nullptr;
    if (!is_child() || pocket_rejected) return;
    for (auto &m : modules) m.clearApi();
    g_hook->restore_zygote_hook(env);
    g_hook->restore_plt_hook();
    // Provider remains mapped: it was loaded cooperatively, not injected.
}
HookContext::HookContext(void *address, size_t size) : start_addr(address), block_size(size) {}
void HookContext::register_hook(dev_t dev, ino_t ino, const char *symbol, void *replacement,
                                void **original) {
    if (lsplt::RegisterHook(dev, ino, symbol, replacement, original))
        plt_backup.emplace_back(dev, ino, symbol, original);
}
void HookContext::hook_plt() {
    cached_map_infos = lsplt::MapInfo::Scan();
    for (const auto &map : cached_map_infos) {
        if (!map.path.ends_with("/libpocket_zygote_api28.so")) continue;
        register_hook(map.dev, map.inode, "fork", reinterpret_cast<void *>(cooperative_fork),
                      reinterpret_cast<void **>(&old_fork));
        break;
    }
    if (!lsplt::CommitHook(cached_map_infos)) old_fork = nullptr;
}
void HookContext::restore_plt_hook() {
    for (auto &[dev, ino, symbol, original] : plt_backup)
        if (*original) lsplt::RegisterHook(dev, ino, symbol, *original, nullptr);
    auto maps = lsplt::MapInfo::Scan();
    lsplt::CommitHook(maps, true);
}
void HookContext::restore_zygote_hook(JNIEnv *env) {
    jclass clazz = env->FindClass(zygote_class);
    if (clazz) {
        env->RegisterNatives(clazz, originals.data(), originals.size());
        env->DeleteLocalRef(clazz);
    }
}
void HookContext::hook_jni_methods(JNIEnv *env, const char *name, JNIMethods methods) {
    jclass clazz = env->FindClass(name);
    if (!clazz) {
        env->ExceptionClear();
        for (auto &m : methods) m.fnPtr = nullptr;
        return;
    }
    auto before = natives(env, clazz);
    for (auto &m : methods) {
        if (!m.fnPtr) continue;
        void *replacement = m.fnPtr;
        if (env->RegisterNatives(clazz, &m, 1) != JNI_OK || env->ExceptionCheck()) {
            env->ExceptionClear();
            m.fnPtr = nullptr;
            continue;
        }
        // ART exposes shorty signatures here. Match the before/after snapshots
        // instead of treating them as JNI descriptors or guessing ArtMethod.
        auto after = natives(env, clazz);
        m.fnPtr = nullptr;
        for (const auto &n : after) {
            if (n.fnPtr != replacement || strcmp(n.name, m.name)) continue;
            for (const auto &o : before) {
                if (!strcmp(n.name, o.name) && !strcmp(n.signature, o.signature)) {
                    m.fnPtr = o.fnPtr;
                    break;
                }
            }
            if (m.fnPtr) break;
        }
    }
    env->DeleteLocalRef(clazz);
}
void hookJniNativeMethods(JNIEnv *env, const char *clazz, JNINativeMethod *methods, int count) {
    if (g_hook && methods && count > 0)
        g_hook->hook_jni_methods(env, clazz, {methods, static_cast<size_t>(count)});
}

extern "C" __attribute__((visibility("default"))) int pocket_zygisk_install(
    JNIEnv *env, const JNINativeMethod *methods, size_t count, const void *art) {
    const char *root = getenv("POCKET_DIRECT_ROOT");
    if (!env || !methods || !count || count > 64 || !art || !root || root[0] != '/' || g_hook)
        return -1;
    callbacks = *static_cast<const ArtCallbacks *>(art);
    if (!callbacks.shorty || !callbacks.count || !callbacks.methods) return -1;
    auto hook = new HookContext(nullptr, 0);
    std::vector<JNINativeMethod> replacements;
    // Resolve the exact method table Pocket supplies, without scanning ART internals.
    for (auto &entry : hook->zygote_methods) {
        auto replacement = entry;
        entry.fnPtr = nullptr;
        for (size_t i = 0; i < count; ++i) {
            if (!strcmp(replacement.name, methods[i].name) &&
                !strcmp(replacement.signature, methods[i].signature)) {
                entry.fnPtr = methods[i].fnPtr;
                replacements.push_back(replacement);
                break;
            }
        }
    }
    bool app = false, server = false;
    for (const auto &method : replacements) {
        app |= !strcmp(method.name, "nativeForkAndSpecialize");
        server |= !strcmp(method.name, "nativeForkSystemServer");
    }
    if (replacements.size() != 2 || !app || !server) {
        fprintf(stderr, "NEO_POCKET_TABLE_REJECTED supplied=%zu matched=%zu\n", count,
                replacements.size());
        delete hook;
        return -1;
    }
    jclass clazz = env->FindClass(zygote_class);
    if (!clazz) {
        env->ExceptionClear();
        delete hook;
        return -1;
    }
    originals.assign(methods, methods + count);
    g_hook = hook;
    hook->hook_plt();
    if (!old_fork ||
        env->RegisterNatives(clazz, replacements.data(), replacements.size()) != JNI_OK) {
        env->ExceptionClear();
        hook->restore_zygote_hook(env);
        hook->restore_plt_hook();
        env->DeleteLocalRef(clazz);
        delete hook;
        g_hook = nullptr;
        return -1;
    }
    env->DeleteLocalRef(clazz);
    fprintf(stderr, "NEO_POCKET_READY version=%s methods=%zu guest_uid=%u\n", ZKSU_VERSION, count,
            getuid());
    return 0;
}
