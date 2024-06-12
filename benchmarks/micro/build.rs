fn main() {
    cc::Build::new()
        .file("../../sqlite-vec.c")
        .compile("sqlite_vec0");
}
