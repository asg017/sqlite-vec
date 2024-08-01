# Using `sqlite-vec` in Rust
[![Crates.io](https://img.shields.io/crates/v/sqlite-vec?logo=rust)](https://crates.io/crates/sqlite-vec)

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
use rusqlite::{ffi::sqlite3_auto_extension, Result};

fn main()-> Result<()> {
    unsafe {
        sqlite3_auto_extension(Some(std::mem::transmute(sqlite3_vec_init as *const ())));
    }
    // future database connection will now automatically include sqlite-vec functions!
    let db = Connection::open_in_memory()?;
    let vec_version: String = db.query_row("select vec_version()", &[v.as_bytes()], |x| x.get(0)?)?;

    println!("vec_version={vec_version}");
    Ok(())
}
```

See
[`simple-rust/demo.rs`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-rust/demo.rs)
for a more complete Rust demo.

## Working with vectors in Rust

If your vectors are provided as a `Vec<f32>` type, the [`zerocopy` crate](https://crates.io/crates/zerocopy) is recommended, specifically `zerocopy::AsBytes`.  This will allow you to pass in vectors into `sqlite-vec` without any copying.

```rs
let query: Vec<f32> = vec![0.1, 0.2, 0.3, 0.4];
let mut stmt = db.prepare("SELECT vec_length(?)")?;
stmt.execute(&[item.1.as_bytes()])?;
```
