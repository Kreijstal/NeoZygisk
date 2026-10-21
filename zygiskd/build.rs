fn main() {
    println!("cargo:rerun-if-changed=../native/empty_tmpfs.c");
    println!("cargo:rerun-if-changed=../native/empty_tmpfs.h");
    cc::Build::new()
        .file("../native/empty_tmpfs.c")
        .include("../native")
        .flag("-std=c18")
        .define("_GNU_SOURCE", None)
        .warnings(true)
        .compile("empty_tmpfs");
}
