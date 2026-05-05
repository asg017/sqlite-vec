#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/**
 * Fuzz target: interleaved insert/update/delete/KNN operations on rescore
 * tables with BOTH quantizer types, exercising the int8 quantizer path
 * and the update code path that the existing rescore-operations.c misses.
 *
 * Key differences from rescore-operations.c:
 * - Tests BOTH bit and int8 quantizers (the existing target only tests bit)
 * - Fuzz-controlled query vectors (not fixed [1,0,0,...])
 * - Exercises the UPDATE path (line 9080+ in sqlite-vec.c)
 * - Tests with 16 dimensions (more realistic, exercises more of the
 *   quantization loop)
 * - Interleaves KNN between mutations to stress the blob_reopen path
 *   when _rescore_vectors rows have been deleted/modified
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) return 0;

  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_stmt *stmtUpdate = NULL;
  sqlite3_stmt *stmtDelete = NULL;
  sqlite3_stmt *stmtKnn = NULL;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* Use first byte to pick quantizer */
  int use_int8 = data[0] & 1;
  data++; size--;

  const char *create_sql = use_int8
    ? "CREATE VIRTUAL TABLE v USING vec0("
      "emb float[16] indexed by rescore(quantizer=int8))"
    : "CREATE VIRTUAL TABLE v USING vec0("
      "emb float[16] indexed by rescore(quantizer=bit))";

  rc = sqlite3_exec(db, create_sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "UPDATE v SET emb = ? WHERE rowid = ?", -1, &stmtUpdate, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? "
    "ORDER BY distance LIMIT 5", -1, &stmtKnn, NULL);

  if (!stmtInsert || !stmtUpdate || !stmtDelete || !stmtKnn)
    goto cleanup;

  size_t i = 0;
  while (i + 2 <= size) {
    uint8_t op = data[i++] % 5; /* 5 operations now */
    uint8_t rowid_byte = data[i++];
    int64_t rowid = (int64_t)(rowid_byte % 24) + 1;

    switch (op) {
      case 0: {
        /* INSERT: consume bytes for 16 floats */
        float vec[16] = {0};
        for (int j = 0; j < 16 && i < size; j++, i++) {
          vec[j] = (float)((int8_t)data[i]) / 8.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 1: {
        /* DELETE */
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 2: {
        /* KNN with fuzz-controlled query vector */
        float qvec[16] = {0};
        for (int j = 0; j < 16 && i < size; j++, i++) {
          qvec[j] = (float)((int8_t)data[i]) / 4.0f;
        }
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_STATIC);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {
          (void)sqlite3_column_int64(stmtKnn, 0);
          (void)sqlite3_column_double(stmtKnn, 1);
        }
        break;
      }
      case 3: {
        /* UPDATE: modify an existing vector (exercises rescore update path) */
        float vec[16] = {0};
        for (int j = 0; j < 16 && i < size; j++, i++) {
          vec[j] = (float)((int8_t)data[i]) / 6.0f;
        }
        sqlite3_reset(stmtUpdate);
        sqlite3_bind_blob(stmtUpdate, 1, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmtUpdate, 2, rowid);
        sqlite3_step(stmtUpdate);
        break;
      }
      case 4: {
        /* INSERT then immediately UPDATE same row (stresses blob lifecycle) */
        float vec1[16] = {0};
        float vec2[16] = {0};
        for (int j = 0; j < 16 && i < size; j++, i++) {
          vec1[j] = (float)((int8_t)data[i]) / 10.0f;
          vec2[j] = -vec1[j]; /* opposite direction */
        }
        /* Insert */
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec1, sizeof(vec1), SQLITE_TRANSIENT);
        if (sqlite3_step(stmtInsert) == SQLITE_DONE) {
          /* Only update if insert succeeded (rowid might already exist) */
          sqlite3_reset(stmtUpdate);
          sqlite3_bind_blob(stmtUpdate, 1, vec2, sizeof(vec2), SQLITE_TRANSIENT);
          sqlite3_bind_int64(stmtUpdate, 2, rowid);
          sqlite3_step(stmtUpdate);
        }
        break;
      }
    }
  }

  /* Final consistency check: full scan must not crash */
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtUpdate);
  sqlite3_finalize(stmtDelete);
  sqlite3_finalize(stmtKnn);
  sqlite3_close(db);
  return 0;
}
