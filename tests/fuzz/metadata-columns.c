#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0("
    "  emb float[4],"
    "  flag boolean metadata,"
    "  count integer metadata,"
    "  score float metadata,"
    "  label text metadata,"
    "  aux_data text auxiliary"
    ")", NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  // Prepare statements for insert and query
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_stmt *stmtKnn = NULL;
  sqlite3_stmt *stmtKnnFilter = NULL;
  sqlite3_stmt *stmtDelete = NULL;

  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb, flag, count, score, label, aux_data) "
    "VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 3",
    -1, &stmtKnn, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? AND flag = 1 LIMIT 3",
    -1, &stmtKnnFilter, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);

  if (!stmtInsert || !stmtKnn || !stmtKnnFilter || !stmtDelete) goto cleanup;

  size_t i = 0;
  while (i + 6 <= size) {
    uint8_t op = data[i++] % 4;
    uint8_t rowid_byte = data[i++];
    int64_t rowid = (int64_t)(rowid_byte % 50) + 1;

    switch (op) {
      case 0: {
        // INSERT with fuzz-derived vector and metadata
        float vec[4];
        for (int j = 0; j < 4 && i < size; j++, i++) {
          vec[j] = (float)((int8_t)data[i]) / 10.0f;
        }
        int flag_val = (i < size) ? data[i++] % 2 : 0;
        int count_val = (i < size) ? (int)((int8_t)data[i++]) : 0;
        float score_val = (i < size) ? (float)((int8_t)data[i++]) / 10.0f : 0.0f;

        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtInsert, 3, flag_val);
        sqlite3_bind_int(stmtInsert, 4, count_val);
        sqlite3_bind_double(stmtInsert, 5, (double)score_val);
        sqlite3_bind_text(stmtInsert, 6, "label", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmtInsert, 7, "aux", -1, SQLITE_STATIC);
        sqlite3_step(stmtInsert);
        break;
      }
      case 1: {
        // KNN query (no filter)
        float qvec[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_STATIC);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 2: {
        // KNN query WITH metadata filter
        float qvec[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        sqlite3_reset(stmtKnnFilter);
        sqlite3_bind_blob(stmtKnnFilter, 1, qvec, sizeof(qvec), SQLITE_STATIC);
        while (sqlite3_step(stmtKnnFilter) == SQLITE_ROW) {}
        break;
      }
      case 3: {
        // DELETE
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
    }
  }

  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtKnn);
  sqlite3_finalize(stmtKnnFilter);
  sqlite3_finalize(stmtDelete);
  sqlite3_close(db);
  return 0;
}
