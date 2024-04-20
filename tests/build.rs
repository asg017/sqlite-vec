use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    cc::Build::new()
        .file("../sqlite-vec.c")
        .include(".")
        .static_flag(true)
        .compile("sqlite-vec-internal");
    println!("cargo:rerun-if-changed=usleep.c");
    println!("cargo:rerun-if-changed=build.rs");
}
