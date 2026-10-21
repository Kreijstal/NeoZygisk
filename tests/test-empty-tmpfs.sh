#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
${CC:-clang} -D_GNU_SOURCE -std=c18 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -g -Inative \
  native/empty_tmpfs.c tests/empty_tmpfs_test.c \
  -Wl,--wrap=open,--wrap=close,--wrap=fstat,--wrap=fstatfs,--wrap=fstatat \
  -Wl,--wrap=fdopendir,--wrap=readdir,--wrap=closedir \
  -o build/tests/empty-tmpfs
build/tests/empty-tmpfs
sh -n module/src/prepare-empty-tmpfs.sh module/src/post-fs-data.sh
cargo test --manifest-path tests/adapter/Cargo.toml
