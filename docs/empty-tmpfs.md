# Preserving an empty tmpfs view

Experimental opt-in support on top of NeoZygisk v2.4, commit
`08080ef775c7fbbd1ad5c4a5d837f81933437abd`.

The ordinary cleanup policy removes every mount with source `magisk`. If this
removes the last view of its tmpfs superblock, its device number disappears from
the application's mountinfo. A freshly mounted tmpfs does not restore that
number. This patch preserves a bind of an empty directory on the original
filesystem instead; it never hardcodes device minor 21.

Both the C++ direct cleanup and Rust cached-namespace cleanup call the same C
validator. The exception requires all of:

- An empty, regular, root-owned, single-link, mode-0600 opt-in file at
  `/data/adb/neozygisk-preserve-empty-tmpfs`, opened without following symlinks.
  Boot preparation labels that file `system_file` so root zygote can read it
  without a broad permission grant over adb_data_file contents.
- Mount root `/.compat-empty`, target `/mnt/compat-empty`, tmpfs type and source
  `magisk` or `tmpfs`.
- Read-only, nosuid, nodev and noexec mount options, confirmed with `fstatfs`.
- An actual, empty, root-owned mode-0555 directory, opened without following
  symlinks, with the same device major/minor as the mountinfo record.

Any failed check leaves the original unmount decision in force. The filesystem
root and Magisk binary/module binds remain subject to normal cleanup. The bind
root contains no directory entries, so it provides no path to sibling files.

## Preparation and rollback

This is disabled by default. The module's post-fs-data script runs the preparation
helper only when the opt-in file exists. After installing a build containing this
patch, an isolated test VM can opt in as root with:

```sh
umask 077
touch /data/adb/neozygisk-preserve-empty-tmpfs
chown 0:0 /data/adb/neozygisk-preserve-empty-tmpfs
chmod 0600 /data/adb/neozygisk-preserve-empty-tmpfs
```

Reboot to create the bind before namespace caching. Preparation refuses occupied
targets, nonempty directories and unexpected ownership/modes. Removing the opt-in
file and rebooting returns to the original policy. The mount directories live in
temporary filesystems. Do not run the helper late and expect existing cached
namespaces to change.

## Source label and coordinated cleanup

Use the companion Magisk v30.7 patch to create the runtime tmpfs with source
`tmpfs` instead of `magisk`. It changes the source argument of the existing mount,
without adding/removing/reordering filesystem creation. A bind then inherits the
neutral label and original device number. Already-mounted filesystems cannot be
renamed with this patch; it needs a build containing the modified magiskinit and
a fresh boot. This is not a procfs filter or an APK patch.

Neutral labels must not disable cleanup. Both NeoZygisk cleanup paths discover
the runtime device from a root-owned `.magisk/config` file on that filesystem,
kernel-reported binary bind roots `/magisk` or `/magiskpolicy`, or the known
empty-view bind. They then select **all tmpfs binds on the same device**, allowing
only the independently validated empty view to survive. Identifying an empty
view for cleanup does not itself grant an exception. Magisk's own cleanup is
also updated to identify its runtime filesystem by the config marker's device.
Legacy `magisk` source matching remains supported. The worker tmpfs keeps its
original label and is removed by existing source matching.

## Validation and limits

`sh tests/test-empty-tmpfs.sh` exercises the actual validator with syscall
wrappers and the Rust adapter with procfs-parsed records. These are synthetic
tests. `sh tests/test-mount-namespace.sh` uses real Linux mounts in fresh
user/mount/PID namespaces, with a temporary tmpfs root and chroot. It runs the
production C++ parser and selector, checks that cleanup removes runtime, binary
and worker mounts, retains only the empty view with its original device and
neutral label, leaves an unrelated tmpfs alone, and rejects a later nonempty
view. Only Android logging is stubbed in that test.

The pair was also built, installed and booted in an ARM64 LineageOS Android 16
Cuttlefish VM. Captures from Revolut's main and protected processes retained
only the empty `0:21` bind with source `tmpfs`. Replaying those captures through
the original native mount checker returned 0 versus baseline 781, with explicit
models for file I/O and unrelated checks. The app still reported reason 754 and
displayed its emulator error. This validates the mount behavior on that VM,
not physical SM-G780G hardware, other Android policies or full banking
compatibility. See the lab validation record for exact results and backups.
