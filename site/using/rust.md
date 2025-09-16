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
SQLite library's `sqlite3_auto_extension()` function.

This example registers sqlite-vec using [rusqlite](https://docs.rs/rusqlite/0.32.1/rusqlite/). First, enable the `"bundled"` feature in your Cargo file entry for rusqlite:

```diff
# Cargo.toml
[dependencies]
+ rusqlite = { version = "VERSION", features = ["bundled"] }
```

Then, you can verify your installation was successful by embedding your first vector. This example uses [zerocopy](https://docs.rs/zerocopy/latest/zerocopy/) to efficiently pass the vector as bytes, and prints the resulting vector and library version as Strings:

```rs
use sqlite_vec::sqlite3_vec_init;
use rusqlite::{ffi::sqlite3_auto_extension, Result};
use zerocopy::AsBytes;

fn main()-> Result<()> {
    unsafe {
        sqlite3_auto_extension(Some(std::mem::transmute(sqlite3_vec_init as *const ())));
    }

    let db = Connection::open_in_memory()?;
    let v: Vec<f32> = vec![0.1, 0.2, 0.3];

    let (vec_version, embedding): (String, String) = db.query_row(
        "select  vec_version(), vec_to_json(?)",
        &[v.as_bytes()],
        |x| Ok((x.get(0)?, x.get(1)?)),
    )?;

    println!("vec_version={vec_version}, embedding={embedding}");
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
