package main

import (
	"bytes"
	"database/sql"
	"encoding/binary"
	"fmt"
	"log"

	sqlite_vec "github.com/asg017/sqlite-vec/bindings/go/cgo"
	_ "github.com/mattn/go-sqlite3"
)

// #cgo LDFLAGS: -L../../dist
import "C"

func serializeFloat32(vector []float32) ([]byte, error) {
	buf := new(bytes.Buffer)
	err := binary.Write(buf, binary.LittleEndian, vector)
	if err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
func main() {
	sqlite_vec.Auto()
	db, err := sql.Open("sqlite3", ":memory:")
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	var sqliteVersion string
	var vecVersion string
	err = db.QueryRow("select sqlite_version(), vec_version()").Scan(&sqliteVersion, &vecVersion)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("sqlite_version=%s, vec_version=%s\n", sqliteVersion, vecVersion)

	_, err = db.Exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[8])")
	if err != nil {
		log.Fatal(err)
	}

	items := map[int][]float32{
		1: {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8},
		2: {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8},
	}

	for id, values := range items {
		v, err := serializeFloat32(values)
		if err != nil {
			log.Fatal(err)
		}
		_, err = db.Exec("INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)", id, v)
		if err != nil {
			log.Fatal(err)
		}
	}

	q := []float32{0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8}
	query, err := serializeFloat32(q)
	if err != nil {
		log.Fatal(err)
	}

	rows, err := db.Query(`
		SELECT
			rowid,
			distance
		FROM vec_items
		WHERE embedding MATCH ?
		ORDER BY distance
		LIMIT 5
	`, query)

	if err != nil {
		log.Fatal(err)
	}

	for rows.Next() {
		var rowid int64
		var distance float64
		err = rows.Scan(&rowid, &distance)
		if err != nil {
			log.Fatal(err)
		}
		fmt.Printf("rowid=%d, distance=%f\n", rowid, distance)
	}
	err = rows.Err()
	if err != nil {
		log.Fatal((err))
	}

}
