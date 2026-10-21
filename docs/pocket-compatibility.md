# Pocket Android 9 compatibility attempt

On 2026-10-21 the `preserve-empty-tmpfs` fork was built for ARM64 Android and
executed inside Pocket's Android 9 guest, hosted by the Pi's LineageOS ARM64
Cuttlefish VM. This was a rootless Pocket process: logical guest root, actual
kernel UID 10177. The outer VM's kernel root was not used to run the daemon.

The daemon's ELF interpreter was changed on a disposable copy to Pocket's
private Android 9 linker. `zygiskd version` returned:

```text
NeoZygisk daemon v2.4-pocket-probe
```

Starting it with `zygiskd --workdir /data/local/tmp/neozygisk-probe` failed:

```text
Zygiskd daemon failed: open mount namespace /proc/1/ns/mnt

Caused by:
    Permission denied (os error 13)
```

The failure occurs before root-provider discovery or module loading. The daemon
currently logs this error and returns exit status zero, so the exit status alone
is not a successful-start check. The version command only verifies loading.

Pocket's guest root does not grant permission to open the outer init's namespace
or call privileged `setns`. Skipping this error would not supply NeoZygisk's other
namespace and injector requirements. This fork therefore does **not** currently
operate unchanged as a Pocket provider. Pocket's existing cooperative zygote
registration interface needs a separate provider adapter before NeoZygisk can
load modules there. This attempt neither injected the outer zygote nor installed
NeoZygisk on a physical phone.

Validation performed:

- Android ARM64 release daemon: `cargo build --offline --release --manifest-path
  zygiskd/Cargo.toml --target aarch64-linux-android --target-dir build/android/rust`
  with NDK r27c compiler/linker and build-time version variables: passed.
- `sh tests/test-empty-tmpfs.sh`: 51 native policy cases and four Rust adapter
  tests passed.
- `sh tests/test-mount-namespace.sh`: real Linux namespace test passed. This is
  a separate Linux test and does not demonstrate Pocket namespace privileges.
