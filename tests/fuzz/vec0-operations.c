#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 6) return 0;

  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_stmt *stmtDelete = NULL;
  sqlite3_stmt *stmtKnn = NULL;
  sqlite3_stmt *stmtScan = NULL;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0(emb float[4])",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 3",
    -1, &stmtKnn, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid FROM v", -1, &stmtScan, NULL);

  if (!stmtInsert || !stmtDelete || !stmtKnn || !stmtScan) goto cleanup;

  size_t i = 0;
  while (i + 2 <= size) {
    uint8_t op = data[i++] % 4;
    uint8_t rowid_byte = data[i++];
    int64_t rowid = (int64_t)(rowid_byte % 32) + 1;

    switch (op) {
      case 0: {
        // INSERT: consume 16 bytes for 4 floats, or use what's left
        float vec[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int j = 0; j < 4 && i < size; j++, i++) {
          vec[j] = (float)((int8_t)data[i]) / 10.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 1: {
        // DELETE
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 2: {
        // KNN query with a fixed query vector
        float qvec[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_STATIC);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 3: {
        // Full scan
        sqlite3_reset(stmtScan);
        while (sqlite3_step(stmtScan) == SQLITE_ROW) {}
        break;
      }
    }
  }

  // Final operations — must not crash regardless of prior state
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtDelete);
  sqlite3_finalize(stmtKnn);
  sqlite3_finalize(stmtScan);
  sqlite3_close(db);
  return 0;
}
