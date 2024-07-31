# `sqlite-vec` statically compiled into WASM builds

You can compile your own version of SQLite's WASM build with `sqlite-vec`
builtin. Dynamically loading SQLite extensions is not supported in the official
WASM build yet, but you can statically compile extensions in. It's not well
documented, but the `sqlite3_wasm_extra_init` option in the SQLite `ext/wasm`
Makefile allows you to inject your own code at initialization time. See the
`Makefile` at the room of the project for more info.

The `wasm.c` file here demonstrates auto-loading the `sqlite-vec` entrypoints at
startup.
