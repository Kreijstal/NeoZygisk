#[cfg(test)]
#[path = "../../../zygiskd/src/empty_tmpfs.rs"]
mod adapter;

#[cfg(test)]
mod tests {
    use super::adapter;
    use procfs::process::MountInfo;
    use std::ffi::{CStr, c_char, c_int};

    // Validate the Rust-to-C ABI arguments independently of the C policy tests.
    #[unsafe(no_mangle)]
    unsafe extern "C" fn nz_keep_empty_tmpfs(
        root: *const c_char,
        target: *const c_char,
        fs_type: *const c_char,
        source: *const c_char,
        options: *const c_char,
        major: u32,
        minor: u32,
    ) -> c_int {
        let get = |s| unsafe { CStr::from_ptr(s).to_str().unwrap() };
        assert_eq!(get(root), "/.compat-empty");
        assert_eq!(get(target), "/mnt/compat-empty");
        assert_eq!(get(fs_type), "tmpfs");
        assert_eq!(get(source), "magisk");
        let mut opts: Vec<_> = get(options).split(',').collect();
        opts.sort_unstable();
        assert_eq!(opts, ["nodev", "noexec", "nosuid", "ro"]);
        assert_eq!(major, 0);
        assert_eq!(minor, 21);
        1
    }

    fn record() -> MountInfo {
        MountInfo::from_line(
            "42 1 0:21 /.compat-empty /mnt/compat-empty ro,nosuid,nodev,noexec - tmpfs magisk ro",
        )
        .unwrap()
    }

    #[unsafe(no_mangle)]
    unsafe extern "C" fn nz_magisk_tmpfs_anchor(
        root: *const c_char,
        target: *const c_char,
        fs_type: *const c_char,
        source: *const c_char,
        major: u32,
        minor: u32,
    ) -> c_int {
        let get = |s| unsafe { CStr::from_ptr(s).to_str().unwrap() };
        assert_eq!(get(root), "/magisk");
        assert_eq!(get(target), "/product/bin/magisk");
        assert_eq!(get(fs_type), "tmpfs");
        assert_eq!(get(source), "tmpfs");
        assert_eq!((major, minor), (0, 21));
        1
    }

    #[test]
    fn neutral_source_anchor_arguments_are_preserved() {
        let info =
            MountInfo::from_line("43 1 0:21 /magisk /product/bin/magisk rw - tmpfs tmpfs rw")
                .unwrap();
        assert!(adapter::is_magisk_anchor(&info));
    }

    #[test]
    fn real_procfs_parser_passes_correct_native_arguments() {
        assert!(adapter::keep_mount(&record()));
    }

    #[test]
    fn malformed_device_numbers_never_reach_native_code() {
        for dev in ["-1:21", "0:-1", "0:4294967296", "0", "0:21:1", "garbage"] {
            let mut info = record();
            info.majmin = dev.into();
            assert!(!adapter::keep_mount(&info));
            assert!(!adapter::is_magisk_anchor(&info));
        }
    }

    #[test]
    fn embedded_nul_is_rejected_before_ffi() {
        let mut info = record();
        info.root.push('\0');
        assert!(!adapter::keep_mount(&info));
        assert!(!adapter::is_magisk_anchor(&info));
    }
}
