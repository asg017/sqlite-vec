module github.com/asg017/sqlite-vec/examples/go

go 1.20

replace github.com/asg017/sqlite-vec/bindings/go/cgo => ../../bindings/go/cgo

require (
	github.com/asg017/sqlite-vec/bindings/go/cgo v0.0.0-00010101000000-000000000000
	github.com/mattn/go-sqlite3 v1.14.22
)
