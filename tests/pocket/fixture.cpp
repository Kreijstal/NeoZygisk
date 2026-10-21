// SPDX-License-Identifier: GPL-3.0-only
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "api.hpp"

static void event(const char *text, int target = -1) {
    int fd =
        open("/data/local/tmp/neo-pocket-events", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0) {
        dprintf(fd, "%s uid=%u target=%d pid=%d\n", text, getuid(), target, getpid());
        close(fd);
    }
}
class Fixture : public zygisk::ModuleBase {
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;
    int directory = -1;
    bool selected = false;
    static inline jboolean (*original)(JNIEnv *, jclass, jstring, jint) = nullptr;
    static inline pid_t (*original_ppid)() = nullptr;
    static pid_t changed_ppid() { return original_ppid() + 1; }
    static jboolean loggable(JNIEnv *env, jclass type, jstring tag, jint priority) {
        event("jniHook");
        return original(env, type, tag, priority);
    }
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        event("onLoad");
    }
    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        event("preServer", args->uid);
    }
    void postServerSpecialize(const zygisk::ServerSpecializeArgs *args) override {
        event("postServer", args->uid);
    }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        selected = name && !strncmp(name, "dev.kreijstal.pocketsyscalltest:",
                                    sizeof("dev.kreijstal.pocketsyscalltest:") - 1);
        if (name) env->ReleaseStringUTFChars(args->nice_name, name);
        if (!selected) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        event("preApp", args->uid);
        directory = api->getModuleDir();
        if (directory < 0 || !api->exemptFd(directory)) _exit(121);
        int fd = openat(directory, "module.prop", O_RDONLY | O_CLOEXEC);
        if (fd < 0) _exit(122);
        close(fd);
        event("moduleDir");
        if (api->getFlags() != 0) _exit(123);
        event("flags");
        errno = 0;
        if (api->connectCompanion() != -1 || errno != ENOTSUP) _exit(124);
        event("unsupportedCompanion");
        JNINativeMethod method{const_cast<char *>("isLoggable"),
                               const_cast<char *>("(Ljava/lang/String;I)Z"),
                               reinterpret_cast<void *>(loggable)};
        api->hookJniNativeMethods(env, "android/util/Log", &method, 1);
        original = reinterpret_cast<decltype(original)>(method.fnPtr);
        if (!original) _exit(125);
        jclass type = env->FindClass("android/util/Log");
        jmethodID mid = env->GetStaticMethodID(type, "isLoggable", "(Ljava/lang/String;I)Z");
        jstring tag = env->NewStringUTF("NeoPocketTest");
        env->CallStaticBooleanMethod(type, mid, tag, 4);
        env->DeleteLocalRef(tag);
        env->DeleteLocalRef(type);
        Dl_info image{};
        struct stat st{};
        if (!dladdr(reinterpret_cast<void *>(changed_ppid), &image) || stat(image.dli_fname, &st))
            _exit(126);
        api->pltHookRegister(st.st_dev, st.st_ino, "getppid",
                             reinterpret_cast<void *>(changed_ppid),
                             reinterpret_cast<void **>(&original_ppid));
        if (!api->pltHookCommit() || !original_ppid || getppid() != syscall(SYS_getppid) + 1)
            _exit(127);
        event("pltHook");
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (!selected) return;
        int fd = openat(directory, "module.prop", O_RDONLY | O_CLOEXEC);
        if (fd < 0) _exit(128);
        close(fd);
        close(directory);
        event("postApp", args->uid);
        event("exemptFd");
    }
};
REGISTER_ZYGISK_MODULE(Fixture)
