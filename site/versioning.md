# Semantic Versioning for `sqlite-vec`

`sqlite-vec` is pre-v1, so according to the rules of
[Semantic Versioning](https://semver.org/), so "minor" release like "0.2.0" or
"0.3.0" may contain breaking changes.

Only SQL functions, table functions, and virtual tables that are defined in the default `sqlite3_vec_init` entrypoint are considered as the `sqlite-vec` API for semantic versioning. This means that other entrypoints and other SQL functions should be considered unstable, untested, and possibly dangerous.

For the SQL API, a "breaking change" would include:

- Removing a function or module
- Changing the number or types of arguments for an SQL function
- Changing the require arguments of position of a table functions
- Changing the `CREATE VIRTUAL TABLE` constructor of a virtual table in a backwards-incompatible way
- Removing columns from a virtual table or table function


The official "bindings" to `sqlite-vec`, including the Python/Node.js/Ruby/Go/Rust are subject to change and are not covered by semantic versioning.
Though I have no plans to change or break them, and would include notes in changelogs if that ever needs to happen.
