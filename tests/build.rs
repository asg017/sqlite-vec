fn main() {
    cc::Build::new()
        .file("../sqlite-vec.c")
        .file("../vendor/sqlite3.c")
        .define("SQLITE_CORE", None)
        .include("../vendor")
        .include("..")
        .static_flag(true)
        .compile("sqlite-vec-internal");
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=../sqlite-vec.c");
}
