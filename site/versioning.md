# Semantic Versioning for `sqlite-vec`

`sqlite-vec` is pre-v1, so according to the rules of
[Semantic Versioning](https://semver.org/), so "minor" release like "0.2.0" or
"0.3.0" may contain breaking changes.

But what exactly counts as a "breaking change" in a SQLite extension? The line
isn't so clear, unforetunately. Here are a all the surfaces that COULD count as
a "breaking change":

- SQL functions and columns on virtual tables
- The C API (extension entrypoints)
- "Bindings" like the official `pip` and `npm` packages
- Release assets like the pre-compile extensions

## What counts as a "breaking change"?

### Changes to SQL functions

- Re-naming or removing an SQL function
- Changing the number of required SQL parameters

### Changes to SQL virtual tables

- The number of

### Changes to the C API

Currently there is no "official" C API for `sqlite-vec`. However, there are
entrypoints defined in C that C developers or developers using FFI can call. Any

### Compile-time options

The removal of any compile time options

## When is `v1.0` coming?

In a few months! The main problems I want to solve before `v1.0` include:

- Metadata columns
- Metadata filtering
- ANN indexing
- Quantization + pre-transformations

Once those items are complete, I will likely create a `v1.0` release, along with
renaming the `vec0` virtual table modile to `vec1`. And if future major releases
are required, a `v2.0` major releases will be made with new `vec2` virtual
tables and so on.

Ideally, only a `v1` major release would be required. But who knows what the
future has in store with vector search!

In general, I will try my best to maximize stability and limit the number of
breaking changes for future `sqlite-vec` versions.
