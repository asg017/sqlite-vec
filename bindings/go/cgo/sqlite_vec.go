package sqlite_vec

// #cgo CFLAGS: -DSQLITE_CORE
// #cgo LDFLAGS: -lm
// #include "../../../sqlite-vec.c"
// #include <sqlite3.h>
import "C"
import (
	"encoding/binary"
	"math"
)

// Auto registers sqlite-vec to be automatically loaded on all new SQLite connections.
// Call this function before opening any database connections.
func Auto() {
	C.sqlite3_auto_extension((*[0]byte)(C.sqlite3_vec_init))
}

// SerializeFloat32 converts a float32 slice into the compact binary format
// that sqlite-vec expects for vector data.
func SerializeFloat32(vector []float32) ([]byte, error) {
	buf := make([]byte, len(vector)*4)
	for i, v := range vector {
		binary.LittleEndian.PutUint32(buf[i*4:], math.Float32bits(v))
	}
	return buf, nil
}

// SerializeInt8 converts an int8 slice into the compact binary format
// that sqlite-vec expects for int8 vector data.
func SerializeInt8(vector []int8) ([]byte, error) {
	buf := make([]byte, len(vector))
	for i, v := range vector {
		buf[i] = byte(v)
	}
	return buf, nil
}
