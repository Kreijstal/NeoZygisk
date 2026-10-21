//! Both namespace cleanup implementations use the same native validation policy.

use procfs::process::MountInfo;
use std::ffi::{CString, c_char, c_int};

unsafe extern "C" {
    fn nz_keep_empty_tmpfs(
        root: *const c_char,
        target: *const c_char,
        fs_type: *const c_char,
        source: *const c_char,
        vfs_options: *const c_char,
        device_major: u32,
        device_minor: u32,
    ) -> c_int;
    fn nz_magisk_tmpfs_anchor(
        root: *const c_char,
        target: *const c_char,
        fs_type: *const c_char,
        source: *const c_char,
        device_major: u32,
        device_minor: u32,
    ) -> c_int;
}

pub fn is_magisk_anchor(info: &MountInfo) -> bool {
    use std::os::unix::ffi::OsStrExt;
    let args = [
        CString::new(info.root.as_bytes()),
        CString::new(info.mount_point.as_os_str().as_bytes()),
        CString::new(info.fs_type.as_bytes()),
        CString::new(info.mount_source.as_deref().unwrap_or("").as_bytes()),
    ];
    let [Ok(root), Ok(target), Ok(fs_type), Ok(source)] = args else {
        return false;
    };
    let Some((major, minor)) = info.majmin.split_once(':') else {
        return false;
    };
    let (Ok(major), Ok(minor)) = (major.parse::<u32>(), minor.parse::<u32>()) else {
        return false;
    };
    unsafe {
        nz_magisk_tmpfs_anchor(
            root.as_ptr(),
            target.as_ptr(),
            fs_type.as_ptr(),
            source.as_ptr(),
            major,
            minor,
        ) == 1
    }
}

pub fn keep_mount(info: &MountInfo) -> bool {
    use std::os::unix::ffi::OsStrExt;
    let options = info
        .mount_options
        .iter()
        .map(|(key, value)| match value {
            Some(value) => format!("{key}={value}"),
            None => key.clone(),
        })
        .collect::<Vec<_>>()
        .join(",");
    let args = [
        CString::new(info.root.as_bytes()),
        CString::new(info.mount_point.as_os_str().as_bytes()),
        CString::new(info.fs_type.as_bytes()),
        CString::new(info.mount_source.as_deref().unwrap_or("").as_bytes()),
        CString::new(options),
    ];
    let [Ok(root), Ok(target), Ok(fs_type), Ok(source), Ok(options)] = args else {
        return false;
    };
    // Fail closed on a malformed device number instead of truncating it.
    let Some((major, minor)) = info.majmin.split_once(':') else {
        return false;
    };
    let (Ok(major), Ok(minor)) = (major.parse::<u32>(), minor.parse::<u32>()) else {
        return false;
    };
    unsafe {
        nz_keep_empty_tmpfs(
            root.as_ptr(),
            target.as_ptr(),
            fs_type.as_ptr(),
            source.as_ptr(),
            options.as_ptr(),
            major,
            minor,
        ) == 1
    }
}
