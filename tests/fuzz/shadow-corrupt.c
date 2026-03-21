#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Build a valid table with 3 vectors (float[4] = 16 bytes each)
  // [1,0,0,0], [0,-1,0,1], [1,1,0,1] as little-endian float32 hex
  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0(emb float[4]);"
    "INSERT INTO v(rowid, emb) VALUES (1, X'0000803f000000000000000000000000');"
    "INSERT INTO v(rowid, emb) VALUES (2, X'00000000000080bf000000000000803f');"
    "INSERT INTO v(rowid, emb) VALUES (3, X'0000803f0000803f000000000000803f');",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return 0;
  }

  // Use first byte to select corruption strategy
  int target = data[0] % 6;
  const uint8_t *payload = data + 1;
  int payload_size = (int)(size - 1);

  sqlite3_stmt *stmt = NULL;

  switch (target) {
    case 0: {
      // Corrupt _chunks validity blob with fuzz data
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_chunks SET validity = ? WHERE rowid = 1", -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
      }
      break;
    }
    case 1: {
      // Corrupt _chunks rowids blob with fuzz data
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_chunks SET rowids = ? WHERE rowid = 1", -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
      }
      break;
    }
    case 2: {
      // Corrupt _vector_chunks00 vectors blob with fuzz data
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_vector_chunks00 SET vectors = ? WHERE rowid = 1", -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
      }
      break;
    }
    case 3: {
      // Set validity to NULL (violates NOT NULL but shadow tables are writable)
      sqlite3_exec(db,
        "UPDATE v_chunks SET validity = NULL WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
    case 4: {
      // Set rowids to NULL
      sqlite3_exec(db,
        "UPDATE v_chunks SET rowids = NULL WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
    case 5: {
      // Delete shadow table rows entirely (orphan the virtual table data)
      sqlite3_exec(db,
        "DELETE FROM v_vector_chunks00 WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
  }

  // Exercise all read paths — NONE should crash
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);
  sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 1", NULL, NULL, NULL);
  sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 2", NULL, NULL, NULL);
  sqlite3_exec(db,
    "SELECT rowid, distance FROM v "
    "WHERE emb MATCH X'0000803f000000000000000000000000' LIMIT 3",
    NULL, NULL, NULL);
  sqlite3_exec(db, "DELETE FROM v WHERE rowid = 2", NULL, NULL, NULL);
  sqlite3_exec(db,
    "INSERT INTO v(rowid, emb) VALUES (4, X'0000803f000000000000000000000000')",
    NULL, NULL, NULL);
  sqlite3_exec(db, "DROP TABLE v", NULL, NULL, NULL);

  sqlite3_close(db);
  return 0;
}
