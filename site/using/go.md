# Using `sqlite-vec` in Go



There are two ways you can embed `sqlite-vec` into Go applications: a CGO option
for libraries like
[`github.com/mattn/go-sqlite3`](https://github.com/mattn/go-sqlite3), or a
WASM-based option with
[`github.com/ncruces/go-sqlite3`](https://github.com/ncruces/go-sqlite3).

## Option 1: CGO {#cgo}

[![Go Reference](https://pkg.go.dev/badge/github.com/asg017/sqlite-vec-go-bindings/cgo.svg)](https://pkg.go.dev/github.com/asg017/sqlite-vec-go-bindings/cgo)

If using [`github.com/mattn/go-sqlite3`](https://github.com/mattn/go-sqlite3) or another CGO-based SQLite library, then use the `github.com/asg017/sqlite-vec-go-bindings/cgo` module to embed `sqlite-vec` into your Go application.

```bash
go get -u github.com/asg017/sqlite-vec-go-bindings/cgo
```

This will compile and statically link `sqlite-vec` into your project. The initial build will be slow, but later builds will be cached and much faster.

Use `sqlite_vec.Auto()` to enable `sqlite-vec` functions in all future database connections. Also `sqlite_vec.Cancel()` is available to undo `Auto()`.

```go
package main

import (
	"database/sql"
	"log"

	sqlite_vec "github.com/asg017/sqlite-vec-go-bindings/cgo"
	_ "github.com/mattn/go-sqlite3"
)

func main() {
	sqlite_vec.Auto()
	db, err := sql.Open("sqlite3", ":memory:")
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	var vecVersion string
	err = db.QueryRow("select vec_version()").Scan(&vecVersion)
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("vec_version=%s\n",vecVersion)
}
```

See
[`simple-go-cgo/demo.go`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-go-cgo/demo.go)
for a more complete Go CGO demo.

## Option 2: WASM based with `ncruces/go-sqlite3` {#ncruces}

[![Go Reference](https://pkg.go.dev/badge/github.com/asg017/sqlite-vec-go-bindings/ncruces.svg)](https://pkg.go.dev/github.com/asg017/sqlite-vec-go-bindings/ncruces)

[`github.com/ncruces/go-sqlite3`](https://github.com/ncruces/go-sqlite3) is an alternative SQLite Go driver that avoids CGO by using a custom WASM build of SQLite. To use `sqlite-vec` from this library, use the specicial WASM binary provided in `github.com/asg017/sqlite-vec-go-bindings/ncruces`.

```bash
go get -u github.com/asg017/sqlite-vec-go-bindings/ncruces
```

```go
package main

import (
	_ "embed"
	"log"

	_ "github.com/asg017/sqlite-vec-go-bindings/ncruces"
	"github.com/ncruces/go-sqlite3"
)

func main() {
	db, err := sqlite3.Open(":memory:")
	if err != nil {
		log.Fatal(err)
	}

	stmt, _, err := db.Prepare(`SELECT vec_version()`)
	if err != nil {
		log.Fatal(err)
	}

	stmt.Step()
	log.Printf("vec_version=%s\n", stmt.ColumnText(0))
	stmt.Close()
}
```

See
[`simple-go-ncruces/demo.go`](https://github.com/asg017/sqlite-vec/blob/main/examples/simple-go-ncruces/demo.go)
for a more complete Go ncruces demo.

The `github.com/asg017/sqlite-vec-go-bindings/ncruces` package embeds a custom WASM build of SQLite, so there's no need to use `github.com/ncruces/go-sqlite3/embed`.


## Working with vectors in Go

If vectors are provided as a list of floats, use `SerializeFloat32(list)` to serialize them into the compact BLOB format that `sqlite-vec` expects.

```go
values := []float32{0.1, 0.1, 0.1, 0.1}
v, err := sqlite_vec.SerializeFloat32(values)
if err != nil {
	log.Fatal(err)
}
_, err = db.Exec("INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)", id, v)
if err != nil {
	log.Fatal(err)
}
```
