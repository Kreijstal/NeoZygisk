#!/bin/sh
# Reuse a completed AOSP/LineageOS build's generated NDK headers and libraries.
# This performs focused ARM64 compilation/checking, not module ZIP packaging.
set -eu
cd "$(dirname "$0")/.."
: "${AOSP_SOURCE:?Set AOSP_SOURCE to the existing LineageOS source directory}"
: "${AOSP_NDK_SYSROOT:=$AOSP_SOURCE/out-vm-arm64/soong/ndk/sysroot}"
: "${AOSP_CLANG:=$AOSP_SOURCE/prebuilts/clang/host/linux-x86/clang-r547379}"
export AOSP_NDK_SYSROOT AOSP_CLANG
mkdir -p build/tests
cat > build/tests/android-cc <<'EOF'
#!/bin/sh
exec "$AOSP_CLANG/bin/clang" --target=aarch64-linux-android26 \
  --sysroot="$AOSP_NDK_SYSROOT" \
  -isystem "$AOSP_NDK_SYSROOT/usr/include/aarch64-linux-android" "$@"
EOF
chmod +x build/tests/android-cc

build/tests/android-cc -D_GNU_SOURCE -std=c18 -Wall -Wextra -Werror \
  -c native/empty_tmpfs.c -o build/tests/empty_tmpfs.android.o
build/tests/android-cc -x c++ -std=c++23 -Wall -Wextra -Werror \
  -fno-exceptions -fno-rtti -Wno-unused-parameter \
  -isystem "$AOSP_CLANG/include/c++/v1" \
  -I native -I loader/src/include \
  -I loader/src/external/lsplt/lsplt/src/main/jni/include \
  -I "$AOSP_SOURCE/libnativehelper/include_jni" \
  -c loader/src/injector/unmount.cpp -o build/tests/unmount.android.o

export CC_aarch64_linux_android="$PWD/build/tests/android-cc"
export AR_aarch64_linux_android="$AOSP_CLANG/bin/llvm-ar"
export MIN_APATCH_VERSION=10762 MIN_KSU_VERSION=10940 MAX_KSU_VERSION=30000
export MIN_MAGISK_VERSION=26402 ZKSU_VERSION=v2.4-empty-tmpfs
export RUSTC="${RUSTC:-$(rustup which rustc)}"
cargo check --manifest-path zygiskd/Cargo.toml --target aarch64-linux-android
