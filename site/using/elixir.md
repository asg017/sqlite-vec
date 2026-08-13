# Using `sqlite-vec` in Elixir

Elixir projects reach SQLite through [`exqlite`](https://hex.pm/packages/exqlite)
(and [`ecto_sqlite3`](https://hex.pm/packages/ecto_sqlite3) on top of it). There
are two ways to bring in `sqlite-vec`: loading it at run time, or compiling it
into the NIF for targets where dynamic loading is unavailable (iOS) or
undesirable.

## Run-time loading

Compile `sqlite-vec` as a loadable extension (see [Compiling](/compiling)) and
point `exqlite` at it:

```elixir
{:ok, conn} = Exqlite.Sqlite3.open(":memory:")
:ok = Exqlite.Sqlite3.enable_load_extension(conn, true)
:ok = Exqlite.Sqlite3.execute(conn, "SELECT load_extension('/path/to/vec0')")
:ok = Exqlite.Sqlite3.enable_load_extension(conn, false)

{:ok, stmt} = Exqlite.Sqlite3.prepare(conn, "SELECT vec_version()")
{:row, [version]} = Exqlite.Sqlite3.step(conn, stmt)
IO.puts(version)
```

With Ecto, load it on every pooled connection instead:

```elixir
config :my_app, MyApp.Repo,
  database: "path/to/db.sqlite3",
  load_extensions: ["/path/to/vec0"]
```

## Static compilation (iOS, hardened deployments)

iOS forbids dynamic code loading, so `load_extension` is not an option there.
`sqlite-vec` supports being compiled directly into the host application via
SQLite's [`SQLITE_EXTRA_INIT`](https://sqlite.org/compile.html#extra_init)
mechanism, and `exqlite` compiles its own bundled `sqlite3.c`, so the pieces
fit: compile `sqlite-vec.c` (the amalgamation) plus an 8-line registration shim
into the NIF.

The shim:

```c
/* vec_init_shim.c */
#include "sqlite3.h"
#include "sqlite-vec.h"

int exqlite_vec_init(const char *unused) {
  (void)unused;
  return sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);
}
```

The build configuration (uses `EXQLITE_EXTRA_SRC`, added to `exqlite` in
[elixir-sqlite/exqlite#355](https://github.com/elixir-sqlite/exqlite/pull/355);
until that lands, the same two lines can be applied to `exqlite`'s Makefile as
a patch):

```elixir
config :exqlite,
  force_build: true,
  make_env: %{
    "EXQLITE_EXTRA_SRC" => "/path/to/sqlite-vec.c /path/to/vec_init_shim.c",
    "EXQLITE_SYSTEM_CFLAGS" =>
      "-DSQLITE_CORE=1 -DSQLITE_EXTRA_INIT=exqlite_vec_init -I/path/to/vendored/headers"
  }
```

After that, every connection has the `vec_*` functions with no loading step:

```elixir
{:ok, conn} = Exqlite.Sqlite3.open(":memory:")

{:ok, stmt} =
  Exqlite.Sqlite3.prepare(conn, "SELECT vec_distance_cosine(vec_f32('[1,0]'), vec_f32('[0,1]'))")

{:row, [1.0]} = Exqlite.Sqlite3.step(conn, stmt)
```

`-DSQLITE_CORE=1` makes `sqlite-vec` compile against the core API rather than
the loadable-extension shim, and `force_build: true` is required so a
precompiled NIF is not fetched instead of building.

## Working with vectors in Elixir

If your embeddings arrive as a list of floats, pack them into the compact
little-endian float32 BLOB format that `sqlite-vec` uses:

```elixir
embedding = [0.1, 0.2, 0.3, 0.4]
blob = for f <- embedding, into: <<>>, do: <<f::float-32-little>>

{:ok, stmt} = Exqlite.Sqlite3.prepare(conn, "SELECT vec_length(?)")
:ok = Exqlite.Sqlite3.bind(stmt, [{:blob, blob}])
{:row, [4]} = Exqlite.Sqlite3.step(conn, stmt)
```

JSON text like `'[0.1, 0.2, 0.3, 0.4]'` also works anywhere a vector is
expected, via `vec_f32('[...]')`.
