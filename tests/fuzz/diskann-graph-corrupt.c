/**
 * Fuzz target for DiskANN shadow table corruption resilience.
 * Creates and populates a DiskANN table, then corrupts shadow table blobs
 * using fuzz data and runs queries.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8))",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  /* Insert a few vectors to create graph structure */
  {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmt, NULL);
    for (int i = 1; i <= 10; i++) {
      float vec[8];
      for (int j = 0; j < 8; j++) {
        vec[j] = (float)i * 0.1f + (float)j * 0.01f;
      }
      sqlite3_reset(stmt);
      sqlite3_bind_int64(stmt, 1, i);
      sqlite3_bind_blob(stmt, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
      sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
  }

  /* Corrupt shadow table data using fuzz bytes */
  size_t offset = 0;

  /* Determine which row and column to corrupt */
  int target_row = (data[offset++] % 10) + 1;
  int corrupt_type = data[offset++] % 3;  /* 0=validity, 1=neighbor_ids, 2=qvecs */

  const char *column_name;
  switch (corrupt_type) {
    case 0: column_name = "neighbors_validity"; break;
    case 1: column_name = "neighbor_ids"; break;
    default: column_name = "neighbor_quantized_vectors"; break;
  }

  /* Read the blob, corrupt it, write it back */
  {
    sqlite3_stmt *readStmt;
    char sqlbuf[256];
    snprintf(sqlbuf, sizeof(sqlbuf),
      "SELECT %s FROM v_diskann_nodes00 WHERE rowid = ?", column_name);
    rc = sqlite3_prepare_v2(db, sqlbuf, -1, &readStmt, NULL);
    if (rc == SQLITE_OK) {
      sqlite3_bind_int64(readStmt, 1, target_row);
      if (sqlite3_step(readStmt) == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(readStmt, 0);
        int blobSize = sqlite3_column_bytes(readStmt, 0);
        if (blob && blobSize > 0) {
          unsigned char *corrupt = sqlite3_malloc(blobSize);
          if (corrupt) {
            memcpy(corrupt, blob, blobSize);
            /* Apply fuzz bytes as XOR corruption */
            size_t remaining = size - offset;
            for (size_t i = 0; i < remaining && i < (size_t)blobSize; i++) {
              corrupt[i % blobSize] ^= data[offset + i];
            }
            /* Write back */
            sqlite3_stmt *writeStmt;
            snprintf(sqlbuf, sizeof(sqlbuf),
              "UPDATE v_diskann_nodes00 SET %s = ? WHERE rowid = ?", column_name);
            rc = sqlite3_prepare_v2(db, sqlbuf, -1, &writeStmt, NULL);
            if (rc == SQLITE_OK) {
              sqlite3_bind_blob(writeStmt, 1, corrupt, blobSize, SQLITE_TRANSIENT);
              sqlite3_bind_int64(writeStmt, 2, target_row);
              sqlite3_step(writeStmt);
              sqlite3_finalize(writeStmt);
            }
            sqlite3_free(corrupt);
          }
        }
      }
      sqlite3_finalize(readStmt);
    }
  }

  /* Run queries on corrupted graph -- should not crash */
  {
    float qvec[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    sqlite3_stmt *knnStmt;
    rc = sqlite3_prepare_v2(db,
      "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = 5",
      -1, &knnStmt, NULL);
    if (rc == SQLITE_OK) {
      sqlite3_bind_blob(knnStmt, 1, qvec, sizeof(qvec), SQLITE_STATIC);
      while (sqlite3_step(knnStmt) == SQLITE_ROW) {}
      sqlite3_finalize(knnStmt);
    }
  }

  /* Full scan */
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

  sqlite3_close(db);
  return 0;
}
