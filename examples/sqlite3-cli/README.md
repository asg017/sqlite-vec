# `sqlite-vec` statically compiled in the SQLite CLI

You can compile your own version of the `sqlite3` CLI with `sqlite-vec` builtin.
The process is not well documented, but the special `SQLITE_EXTRA_INIT` compile
option can be used to "inject" code at initialization time. See the `Makefile`
at the root of this project for some more info.

The `core_init.c` file here demonstrates auto-loading the `sqlite-vec`
entrypoints at startup.
