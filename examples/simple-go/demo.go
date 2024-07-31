package main

import (
	"database/sql"
	"fmt"
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

	var sqliteVersion string
	var vecVersion string
	err = db.QueryRow("select sqlite_version(), vec_version()").Scan(&sqliteVersion, &vecVersion)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("sqlite_version=%s, vec_version=%s\n", sqliteVersion, vecVersion)

	_, err = db.Exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])")
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

	for id, values := range items {
		v, err := sqlite_vec.SerializeFloat32(values)
		if err != nil {
			log.Fatal(err)
		}
		_, err = db.Exec("INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)", id, v)
		if err != nil {
			log.Fatal(err)
		}
	}

	query, err := sqlite_vec.SerializeFloat32(q)
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
		LIMIT 3
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
