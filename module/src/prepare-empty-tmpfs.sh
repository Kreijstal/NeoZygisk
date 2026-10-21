#!/system/bin/sh
# Experimental, opt-in preparation. Run before zygote/clean namespaces are cached.
set -eu

flag=/data/adb/neozygisk-preserve-empty-tmpfs
target=/mnt/compat-empty

[ "$(id -u)" = 0 ] || exit 1
[ -f "$flag" ] && [ ! -L "$flag" ] || exit 1
[ "$(stat -c '%u:%g:%a:%s:%h' "$flag")" = '0:0:600:0:1' ] || exit 1
# Zygote reads the opt-in before dropping root. Keep its DAC restrictions while
# allowing that read without granting broad access to adb_data_file contents.
chcon u:object_r:system_file:s0 "$flag"

runtime=$(magisk --path)
case "$runtime" in
  /*) [ "$runtime" != / ] || exit 1 ;;
  *) exit 1 ;;
esac
runtime=$(readlink -f "$runtime")
[ "$(stat -f -c '%t' "$runtime")" = 1021994 ] || exit 1
source=$runtime/.compat-empty

prepare_dir() {
  if [ ! -e "$1" ] && [ ! -L "$1" ]; then
    mkdir -m 0555 "$1"
    chown 0:0 "$1"
  fi
  [ -d "$1" ] && [ ! -L "$1" ] || return 1
  [ "$(stat -c '%u:%g:%a' "$1")" = '0:0:555' ] || return 1
  contents=$(ls -A "$1") || return 1
  [ -z "$contents" ] || return 1
}

# Refuse existing mounts rather than stacking over another view.
if awk -v target="$target" '$5 == target { found=1 } END { exit !found }' /proc/self/mountinfo; then
  exit 1
fi
prepare_dir "$source"
prepare_dir "$target"

mount -o bind "$source" "$target"
# Undo the bind on failure, never leaving a writable exception candidate.
if ! mount --make-private "$target" ||
   ! mount -o remount,bind,ro,nosuid,nodev,noexec "$source" "$target"; then
  umount "$target"
  exit 1
fi
[ "$(stat -c '%d' "$source")" = "$(stat -c '%d' "$target")" ] || {
  umount "$target"
  exit 1
}
