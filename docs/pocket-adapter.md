# Cooperative Pocket adapter

This fork has a separate ARM64 Android 9 provider target. Pocket loads it through
its existing `pocket_zygisk_install` entry point. It reuses NeoZygisk's module ABI
and specialization lifecycle, with a userspace backend instead of the privileged
ptrace loader and Rust daemon. No Pocket APK modification is required.

## Build

```sh
python3 tools/build-pocket.py --ndk /path/to/android-ndk-r27c
```

Outputs:

- `build/pocket/libneozygisk_pocket.so`
- `build/pocket/neozygisk-pocket-0.1.zip`, an optional provider module
- `build/pocket/libneo_pocket_fixture.so`, the dedicated validation module

The ZIP contains the provider at `system/lib64/libpocket_zygisk.so`. It contains
no Magisk binary, ptracer, Rust daemon, or application profiles. Install it with
Pocket's optional Magisk runtime, enable guest Zygisk, and restart the guest.
The runtime must support user-supplied providers in its merged module view:
[Kreijstal/Magisk pocket-api28, commit 4afd5e5](https://github.com/Kreijstal/Magisk/commit/4afd5e5)
or later.
Disable the provider module and restart to restore the default provider.
This package is for the Pocket guest, not the physical host's root manager.

Magisk currently supplies installation, module upgrade/scripts, and the merged
filesystem view. NeoZygisk supplies the loaded Zygisk provider. Module discovery
and callbacks do not connect to the Magisk daemon or NeoZygisk's privileged daemon.
A standalone installer/lifecycle without Magisk has not been implemented here.

## Behavior

- Exact JNI method-table matching for Pocket's app and system-server fork hooks.
  Other methods in Pocket's table remain registered normally.
- App and system-server onLoad/pre/post callbacks through NeoZygisk's module ABI.
- Direct loading of enabled ARM64 module libraries from `/data/adb/modules`.
  `disable` and `remove` markers are respected in fresh processes.
- Public JNI hooking uses the ART callbacks Pocket supplies and verifies. The
  adapter does not infer private ArtMethod layouts.
- Public native PLT hooking, module-directory access, and FD exemption.
- Modules can request `DLCLOSE_MODULE_LIBRARY`; the provider itself stays mapped.

Module names are sorted before assigning process-local IDs. A provider is loaded
once per zygote; module changes require fresh app processes. Changing or disabling
the provider requires a guest restart. The default ptrace target remains separate.

## Explicit limits

`connectCompanion` returns -1 with ENOTSUP. `FORCE_DENYLIST_UNMOUNT` and mount
namespace switching are unsupported and logged; no successful unmount is faked.
Process flags report no root grant or denylist isolation. Kernel root, mount
namespaces, SELinux changes, trace concealment, and privileged injection are not
supplied. Pocket's existing child-zygote rejection remains in effect.

This initially targets ARM64 Pocket Android 9 and Zygisk module API versions 1–5;
validation uses API 5. It does not establish compatibility with arbitrary modules.
Modules requiring companions or kernel facilities need additional work. Guest
logical UID changes do not grant host kernel privileges or create separate kernel
UID isolation between guest apps.

## Focused validation

```sh
cmake --build build/pocket --target neozygisk_pocket neo_pocket_fixture -j 4
cmake --build build/android/arm64-v8a --target zygisk -j 4
```

The existing Pocket real-APK fixture compares selected and unselected processes
using libc and inline ARM64 SVC to open a physical fixture file. It runs inside
Pocket inside the Pi's LineageOS Cuttlefish VM. Actual kernel UID must remain
nonzero. The additional `tests/pocket/fixture.cpp` module checks server and app
callbacks, a real Java native-method hook, a real PLT hook, getModuleDir across
specialization, FD exemption, and explicit companion rejection.

`tests/pocket/verify.py` uses an existing paired owner console and the guarded Pi
ADB helper. It reads bounded logs and the fresh APK report. It never prints the
private console token. No host root is used by the app or verification calls.

## Pi result, 2026-10-21

Passed on Pocket 0.30 with actual kernel UID 10177 and guest app UID 10019:

- Cold boot: `NEO_POCKET_READY ... methods=6 guest_uid=0` and
  `POCKET_ZYGISK_INSTALL result=0`.
- Server callbacks observed before and after logical specialization to UID 1000.
- Both dedicated app processes passed app callbacks, real JNI and PLT hooks,
  module-directory access and FD preservation across specialization.
- Selected direct SVC openat succeeded; the unselected process retained ENOENT.
- Disabling the syscall module restored ENOENT in both fresh processes;
  re-enabling restored translation only in the selected process. Profile data was
  preserved, with no outer-VM root used for these test operations.

Structured evidence: [pocket-validation.json](pocket-validation.json) and
[pocket-toggle-validation.json](pocket-toggle-validation.json). These are VM
runtime tests, not physical Samsung validation or broad module compatibility.

For the extra API fixture, use the paired owner console to create
`/data/adb/modules/neo_adapter_fixture/zygisk`, upload `tests/pocket/module.prop`
to its module root and `build/pocket/libneo_pocket_fixture.so` as
`zygisk/arm64-v8a.so`. Remove its disable marker and restart the guest to exercise
server callbacks. The already-installed two-process APK fixture and
`test_syscall_app_on_pi.py` come from
[Kreijstal/aosp-pocket](https://github.com/Kreijstal/aosp-pocket).

Copy `tests/pocket/verify.py` as `/home/pi/verify-neo-pocket.py` and
`tests/pocket/toggle_on_pi.py` as `/home/pi/toggle-neo-pocket.py`, then run:

```sh
python3 /home/pi/test_syscall_app_on_pi.py --launch-existing --output /home/pi/neo-pocket-app-validation.json
python3 /home/pi/verify-neo-pocket.py
python3 /home/pi/toggle-neo-pocket.py
```

The verifier expects a fresh boot's event file and the two fresh fixture
processes. The toggle test checks each PID's exact fixture cmdline before
terminating it, uses the owner console, and preserves module configuration.
The dedicated validation module is disabled after the final checks; the Neo
provider and syscall profile module remain enabled on the Pi.
