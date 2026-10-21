#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
: "${JNI_HEADERS:=/usr/lib/jvm/java-21-openjdk/include}"
clang -D_GNU_SOURCE -std=c18 -Wall -Wextra -Werror -Inative \
  -c native/empty_tmpfs.c -o build/tests/empty_tmpfs.host.o
clang++ -O2 -std=c++23 -Wno-unused-parameter -Wno-writable-strings -Itests/stubs -Inative \
  -Iloader/src/include -Iloader/src/injector \
  -Iloader/src/external/lsplt/lsplt/src/main/jni/include \
  -I"$JNI_HEADERS" -I"$JNI_HEADERS/linux" \
  tests/mount_namespace_test.cpp loader/src/injector/unmount.cpp \
  build/tests/empty_tmpfs.host.o -o build/tests/mount-namespace
sandbox=$(mktemp -d "$PWD/build/tests/ns.XXXXXX")
trap 'rmdir "$sandbox"' EXIT HUP INT TERM
unshare -Urnmp --fork -- build/tests/mount-namespace "$sandbox"
