# Using `sqlite-vec` in Go

There are two ways you can embed `sqlite-vec` into Go applications: a CGO option
for libraries like
[`github.com/mattn/go-sqlite3`](https://github.com/mattn/go-sqlite3), or a
WASM-based option with
[`github.com/ncruces/go-sqlite3`](https://github.com/ncruces/go-sqlite3).

## Option 1: CGO

```bash
go get -u github.com/asg017/sqlite-vec/bindings/go/cgo
```

## Option 2: WASM based with `ncruces/go-sqlite3`

```
go
```

## Working with vectors in Go
