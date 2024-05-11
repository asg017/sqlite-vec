package vec

// #cgo LDFLAGS: -lsqlite_vec0
// #cgo CFLAGS: -DSQLITE_CORE
// #include <sqlite3ext.h>
//
// extern int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);
//
import "C"

// Once called, every future new SQLite3 connection created in this process
// will have the hello extension loaded. It will persist until [Cancel] is
// called.
//
// Calls [sqlite3_auto_extension()] under the hood.
//
// [sqlite3_auto_extension()]: https://www.sqlite.org/c3ref/auto_extension.html
func Auto() {
	C.sqlite3_auto_extension( (*[0]byte) ((C.sqlite3_vec_init)) );
}

// "Cancels" any previous calls to [Auto]. Any new SQLite3 connections created
// will not have the hello extension loaded.
//
// Calls sqlite3_cancel_auto_extension() under the hood.
func Cancel() {
	C.sqlite3_cancel_auto_extension( (*[0]byte) (C.sqlite3_vec_init) );
}
