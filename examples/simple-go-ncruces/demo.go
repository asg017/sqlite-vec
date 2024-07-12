package main

import (
	"bytes"
	_ "embed"
	"encoding/binary"
	"fmt"
	"log"

	"github.com/ncruces/go-sqlite3"
)

//go:embed sqlite3.vec.wasm
var sqliteWithVecWasm []byte

func serializeFloat32(vector []float32) ([]byte, error) {
	buf := new(bytes.Buffer)
	err := binary.Write(buf, binary.LittleEndian, vector)
	if err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}


func main() {
	sqlite3.Binary = sqliteWithVecWasm

	db, err := sqlite3.Open(":memory:")
	if err != nil {
		log.Fatal(err)
	}

	stmt, _, err := db.Prepare(`SELECT sqlite_version(), vec_version()`)
	if err != nil {
		log.Fatal(err)
	}

	stmt.Step()

	fmt.Printf("sqlite_version=%s, vec_version=%s\n", stmt.ColumnText(0), stmt.ColumnText(1))


	err = db.Exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])")
	if err != nil {
		log.Fatal(err)
	}
	items := map[int][]float32{
		1: {0.1, 0.1, 0.1, 0.1},
		2: {0.2, 0.2, 0.2, 0.2},
		3: {0.3, 0.3, 0.3, 0.3},
		4: {0.4, 0.4, 0.4, 0.4},
		5: {0.5, 0.5, 0.5, 0.5},
	}
	q := []float32{0.3, 0.3, 0.3, 0.3}

	stmt, _, err = db.Prepare("INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)")
	if err != nil {
		log.Fatal(err)
	}

	for id, values := range items {
		v, err := serializeFloat32(values)
		if err != nil {
			log.Fatal(err)
		}
		stmt.BindInt(1, id)
		stmt.BindBlob(2, v)
		err = stmt.Exec()
		if err != nil {
			log.Fatal(err)
		}
		stmt.Reset()
	}
	stmt.Close()



	stmt, _, err = db.Prepare(`
		SELECT
			rowid,
			distance
		FROM vec_items
		WHERE embedding MATCH ?
		ORDER BY distance
		LIMIT 3
	`);

	if err != nil {
		log.Fatal(err)
	}

	query, err := serializeFloat32(q)
	if err != nil {
		log.Fatal(err)
	}
	stmt.BindBlob(1, query)

	for stmt.Step() {
		rowid := stmt.ColumnInt64(0)
		distance := stmt.ColumnFloat(1)
		fmt.Printf("rowid=%d, distance=%f\n", rowid, distance)
	}
	if err := stmt.Err(); err != nil {
		log.Fatal(err)
	}

	err = stmt.Close()
	if err != nil {
		log.Fatal(err)
	}

	err = db.Close()
	if err != nil {
		log.Fatal(err)
	}
}
