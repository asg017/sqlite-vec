/**
 * Fuzz target for DiskANN buffered insert and flush paths.
 *
 * When buffer_threshold > 0, inserts go into a flat buffer table and
 * are flushed into the graph in batch. This fuzzer exercises:
 *
 *   - diskann_buffer_write / diskann_buffer_delete / diskann_buffer_exists
 *   - diskann_flush_buffer (batch graph insertion)
 *   - diskann_insert with buffer_threshold (batching logic)
 *   - Buffer-graph merge in vec0Filter_knn_diskann (unflushed vectors
 *     must be scanned during KNN and merged with graph results)
 *   - Delete of a buffered (not yet flushed) vector
 *   - Delete of a graph vector while buffer has pending inserts
 *   - Interaction: insert to buffer, query (triggers buffer scan), flush,
 *     query again (now from graph)
 *
 * The buffer merge path in vec0Filter_knn_diskann is particularly
 * interesting because it does a brute-force scan of buffer vectors and
 * merges with the top-k from graph search.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

static uint8_t fuzz_byte(const uint8_t **data, size_t *size, uint8_t def) {
  if (*size == 0) return def;
  uint8_t b = **data;
  (*data)++;
  (*size)--;
  return b;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) return 0;

  int rc;
  sqlite3 *db;
  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* buffer_threshold: small (3-8) to trigger frequent flushes */
  int buf_threshold = 3 + (fuzz_byte(&data, &size, 0) % 6);
  int dims = 8;

  char sql[512];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] INDEXED BY diskann("
    "neighbor_quantizer=binary, n_neighbors=8, "
    "search_list_size=16, buffer_threshold=%d"
    "))", dims, buf_threshold);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_stmt *stmtInsert = NULL, *stmtDelete = NULL, *stmtKnn = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = ?",
    -1, &stmtKnn, NULL);

  if (!stmtInsert || !stmtDelete || !stmtKnn) goto cleanup;

  float vec[8];
  int next_rowid = 1;

  while (size >= 2) {
    uint8_t op = fuzz_byte(&data, &size, 0) % 6;
    uint8_t param = fuzz_byte(&data, &size, 0);

    switch (op) {
      case 0: { /* Insert: accumulates in buffer until threshold */
        int64_t rowid = next_rowid++;
        if (next_rowid > 64) next_rowid = 1; /* wrap around for reuse */
        for (int j = 0; j < dims; j++) {
          vec[j] = (float)((int8_t)fuzz_byte(&data, &size, 0)) / 10.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 1: { /* KNN query while buffer may have unflushed vectors */
        for (int j = 0; j < dims; j++) {
          vec[j] = (float)((int8_t)fuzz_byte(&data, &size, 0)) / 10.0f;
        }
        int k = (param % 10) + 1;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, k);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 2: { /* Delete a potentially-buffered vector */
        int64_t rowid = (int64_t)(param % 64) + 1;
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 3: { /* Insert several at once to trigger flush mid-batch */
        for (int i = 0; i < buf_threshold + 1 && size >= 2; i++) {
          int64_t rowid = (int64_t)(fuzz_byte(&data, &size, 0) % 64) + 1;
          for (int j = 0; j < dims; j++) {
            vec[j] = (float)((int8_t)fuzz_byte(&data, &size, 0)) / 10.0f;
          }
          sqlite3_reset(stmtInsert);
          sqlite3_bind_int64(stmtInsert, 1, rowid);
          sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
          sqlite3_step(stmtInsert);
        }
        break;
      }
      case 4: { /* Insert then immediately delete (still in buffer) */
        int64_t rowid = (int64_t)(param % 64) + 1;
        for (int j = 0; j < dims; j++) vec[j] = 0.1f * param;
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);

        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 5: { /* Query with k=0 and k=1 (boundary) */
        for (int j = 0; j < dims; j++) vec[j] = 0.0f;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, param % 2); /* k=0 or k=1 */
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
    }
  }

  /* Final query to exercise post-operation state */
  {
    float qvec[8] = {1.0f, -1.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
    sqlite3_reset(stmtKnn);
    sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtKnn, 2, 20);
    while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
  }

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtDelete);
  sqlite3_finalize(stmtKnn);
  sqlite3_close(db);
  return 0;
}
