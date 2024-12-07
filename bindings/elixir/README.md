# SqliteVec

[![Hex Package](https://img.shields.io/hexpm/v/sqlite_vec.svg?style=for-the-badge)](https://hex.pm/packages/sqlite_vec)
[![Hex Docs](https://img.shields.io/badge/hex-docs-blue.svg?style=for-the-badge)](https://hexdocs.pm/sqlite_vec)
[![Build Status](https://img.shields.io/github/actions/workflow/status/joelpaulkoch/sqlite_vec/ci.yml?label=Build%20Status&style=for-the-badge&branch=main)](https://github.com/joelpaulkoch/sqlite_vec/actions)

A wrapper to use [sqlite-vec](https://github.com/asg017/sqlite-vec), a SQLite extension for working with vectors, in Elixir.
The configured version of the precompiled loadable library will be downloaded from the GitHub releases.
Moreover, this package provides structs and custom Ecto types for working with Float32, Int8, and Bit vectors.

## Limitations
- it's currently not possible to create int8 and bit vectors using `Ecto`. You must directly use SQL to do so
- not implemented operations: `vec_each`, `vec_quantize_i8`

## Installation

The package can be installed by adding `sqlite_vec` to your list of dependencies in `mix.exs`:

```elixir
def deps do
  [
    {:sqlite_vec, "~> 0.1.0"}
  ]
end
```

## Getting Started

`SqliteVec.path/0` returns the path of the downloaded library.
Therefore, you can load the extension using this path.

For instance with `Exqlite`:
```elixir
{:ok, conn} = Basic.open(":memory:")
:ok = Basic.enable_load_extension(conn)

Basic.load_extension(conn, SqliteVec.path())
```

Or, with an `Ecto.Repo` and `ecto_sqlite3`:

```elixir
defmodule MyApp.Repo do
  use Ecto.Repo,
    otp_app: :my_app,
    adapter: Ecto.Adapters.SQLite3
end

config :my_app, MyApp.Repo, load_extensions: [SqliteVec.path()]
```

You can check out the [Getting Started](notebooks/getting_started.livemd) and [Usage with Ecto](notebooks/usage_with_ecto.livemd) notebooks.

## Attribution

Special thanks to these projects that helped to make this package:

- [OctoFetch](https://hexdocs.pm/octo_fetch/readme.html) which does all the work for downloading the GitHub releases, and served as a blueprint for this package (yes, including this Attribution section :) )
- [sqlite-vec](https://github.com/asg017/sqlite-vec), of course, which provides all of the functionality
- [pgvector](https://hexdocs.pm/pgvector/readme.html) provides something similar for postgres and quite some code could be reused
