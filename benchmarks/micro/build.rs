fn main() {
    cc::Build::new()
        .file("../../sqlite-vec.c")
        .include("../../vendor")
        .include("../../")
        .compile("sqlite_vec0");
}
