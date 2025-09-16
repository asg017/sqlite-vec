# Installing

You have several options to include `sqlite-vec` into your projects, including
PyPi packages for Python, NPM packages for Node.js, Gems for Ruby, and more.

## With popular package managers

::: code-group

```bash [Python]
pip install sqlite-vec
```

```bash [Node.js]
npm install sqlite-vec
```

```bash [Bun]
bun install sqlite-vec
```

```bash [Deno]
deno add npm:sqlite-vec
```

```bash [Ruby]
gem install sqlite-vec
```

```bash [Rust]
cargo add sqlite-vec
```

```bash [Go (CGO)]
go get -u github.com/asg017/sqlite-vec-go-bindings/cgo
```
```bash [Go (ncruces WASM)]
go get -u github.com/asg017/sqlite-vec-go-bindings/ncruces
```

```bash [Datasette]
datasette install datasette-sqlite-vec
```

```bash [sqlite-utils]
sqlite-utils install sqlite-utils-sqlite-vec
```

:::

## Pre-compiled extensions

Alternatively, you can download pre-compiled loadable extensions from the
[`sqlite-vec` Github Releases](https://github.com/asg017/sqlite-vec/releases/latest).

There's also an `install.sh` script that will automatically download the appropriate pre-compiled extension from Github Releases to your machine.


```sh
# yolo
curl -L 'https://github.com/asg017/sqlite-vec/releases/latest/download/install.sh' | sh
```

```sh
# ok lets play it safe
curl -o install.sh -L https://github.com/asg017/sqlite-vec/releases/latest/download/install.sh
# inspect your scripts
cat install.sh
# TODO Test if execute permissions?
./install.sh
```


## Compiling

`sqlite-vec` is a single `sqlite-vec.c` and `sqlite-vec.h`, and can be easily compiled for different platforms, or statically linked into larger applications.

See [*Compiling `sqlite-vec`*](#compiling) for more information.
