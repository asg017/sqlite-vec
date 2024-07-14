# Using `sqlite-vec` in Rust

You can embed `sqlite-vec` into your Rust projects using the official
[`sqlite-vec` crate](https://crates.io/crates/sqlite-vec).

```bash
cargo add sqlite-vec
```

The crate embeds the `sqlite-vec` C source code, and uses the
[`cc` crate](https://crates.io/crates/sqlite-vec) to compile and statically link
`sqlite-vec` at build-time.

The `sqlite-vec` crate exposes a single function `sqlite3_vec_init`, which is
the C entrypoint for the SQLite extension. You can "register" with your Rust
SQLite library's `sqlite3_auto_extension()` function. Here's an example with
`rusqlite`:

```rs
use sqlite_vec::sqlite3_vec_init;
use rusqlite::{ffi::sqlite3_auto_extension};

fn main() {
    unsafe {
        sqlite3_auto_extension(Some(std::mem::transmute(sqlite3_vec_init as *const ())));
    }
    // future database connection will now automatically include sqlite-vec functions!
}
```

A full [`sqlite-vec` Rust demo](#TODO) is also available.

## Working with vectors in Rust
