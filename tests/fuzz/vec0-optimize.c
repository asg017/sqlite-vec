#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/*
 * Fuzz target for the vec0 optimize command.
 * Performs random INSERT/DELETE operations, then runs optimize,
 * and asserts that all remaining rows are still queryable and
 * the virtual table is in a consistent state.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 4) return 0;

  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_stmt *stmtDelete = NULL;
  sqlite3_stmt *stmtScan = NULL;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0(emb float[4], chunk_size=4)",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, emb FROM v", -1, &stmtScan, NULL);

  if (!stmtInsert || !stmtDelete || !stmtScan) goto cleanup;

  /* Track which rowids are live */
  uint8_t live[16];
  memset(live, 0, sizeof(live));

  size_t i = 0;
  while (i + 2 <= size - 2) {  /* reserve 2 bytes for optimize trigger */
    uint8_t op = data[i++] % 3;
    uint8_t rowid_byte = data[i++];
    int64_t rowid = (int64_t)(rowid_byte % 16) + 1;

    switch (op) {
      case 0: {
        /* INSERT */
        float vec[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int j = 0; j < 4 && i < size - 2; j++, i++) {
          vec[j] = (float)((int8_t)data[i]) / 10.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        rc = sqlite3_step(stmtInsert);
        if (rc == SQLITE_DONE) {
          live[rowid - 1] = 1;
        }
        break;
      }
      case 1: {
        /* DELETE */
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        rc = sqlite3_step(stmtDelete);
        if (rc == SQLITE_DONE) {
          live[rowid - 1] = 0;
        }
        break;
      }
      case 2: {
        /* Full scan */
        sqlite3_reset(stmtScan);
        while (sqlite3_step(stmtScan) == SQLITE_ROW) {}
        break;
      }
    }
  }

  /* Run optimize */
  rc = sqlite3_exec(db, "INSERT INTO v(v) VALUES ('optimize')", NULL, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* Verify: all live rows are still queryable */
  int expected_count = 0;
  for (int j = 0; j < 16; j++) {
    if (live[j]) expected_count++;
  }

  sqlite3_stmt *stmtCount = NULL;
  sqlite3_prepare_v2(db, "SELECT count(*) FROM v", -1, &stmtCount, NULL);
  if (stmtCount) {
    rc = sqlite3_step(stmtCount);
    assert(rc == SQLITE_ROW);
    int actual_count = sqlite3_column_int(stmtCount, 0);
    assert(actual_count == expected_count);
    sqlite3_finalize(stmtCount);
  }

  /* Verify each live row is accessible via point query */
  sqlite3_stmt *stmtPoint = NULL;
  sqlite3_prepare_v2(db, "SELECT emb FROM v WHERE rowid = ?", -1, &stmtPoint, NULL);
  if (stmtPoint) {
    for (int j = 0; j < 16; j++) {
      if (!live[j]) continue;
      sqlite3_reset(stmtPoint);
      sqlite3_bind_int64(stmtPoint, 1, j + 1);
      rc = sqlite3_step(stmtPoint);
      assert(rc == SQLITE_ROW);
      assert(sqlite3_column_bytes(stmtPoint, 0) == 16);
    }
    sqlite3_finalize(stmtPoint);
  }

  /* Verify shadow table consistency: _rowids count matches live count */
  sqlite3_stmt *stmtRowids = NULL;
  sqlite3_prepare_v2(db, "SELECT count(*) FROM v_rowids", -1, &stmtRowids, NULL);
  if (stmtRowids) {
    rc = sqlite3_step(stmtRowids);
    assert(rc == SQLITE_ROW);
    assert(sqlite3_column_int(stmtRowids, 0) == expected_count);
    sqlite3_finalize(stmtRowids);
  }

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtDelete);
  sqlite3_finalize(stmtScan);
  sqlite3_close(db);
  return 0;
}
