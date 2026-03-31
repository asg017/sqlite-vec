/**
 * Fuzz target for DiskANN shadow table blob size mismatches.
 *
 * The critical vulnerability: diskann_node_read() copies whatever blob size
 * SQLite returns, but diskann_search/insert/delete index into those blobs
 * using cfg->n_neighbors * sizeof(i64) etc. If the blob is truncated,
 * extended, or has wrong size, this causes out-of-bounds reads/writes.
 *
 * This fuzzer:
 *   1. Creates a valid DiskANN graph with several nodes
 *   2. Uses fuzz data to directly write malformed blobs to shadow tables:
 *      - Truncated neighbor_ids (fewer bytes than n_neighbors * 8)
 *      - Truncated validity bitmaps
 *      - Oversized blobs with garbage trailing data
 *      - Zero-length blobs
 *      - Blobs with valid headers but corrupted neighbor rowids
 *   3. Runs INSERT, DELETE, and KNN operations that traverse the corrupted graph
 *
 * Key code paths targeted:
 *   - diskann_node_read with mismatched blob sizes
 *   - diskann_validity_get / diskann_neighbor_id_get on truncated blobs
 *   - diskann_add_reverse_edge reading corrupted neighbor data
 *   - diskann_repair_reverse_edges traversing corrupted neighbor lists
 *   - diskann_search iterating neighbors from corrupted blobs
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
  if (size < 32) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* Use binary quantizer, float[16], n_neighbors=8 for predictable blob sizes:
   *   validity: 8/8 = 1 byte
   *   neighbor_ids: 8 * 8 = 64 bytes
   *   qvecs: 8 * (16/8) = 16 bytes  (binary: 2 bytes per qvec)
   */
  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[16] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8))",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  /* Insert 12 vectors to create a valid graph structure */
  {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmt, NULL);
    for (int i = 1; i <= 12; i++) {
      float vec[16];
      for (int j = 0; j < 16; j++) {
        vec[j] = (float)i * 0.1f + (float)j * 0.01f;
      }
      sqlite3_reset(stmt);
      sqlite3_bind_int64(stmt, 1, i);
      sqlite3_bind_blob(stmt, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
      sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
  }

  /* Now corrupt shadow table blobs using fuzz data */
  const char *columns[] = {
    "neighbors_validity",
    "neighbor_ids",
    "neighbor_quantized_vectors"
  };

  /* Expected sizes for n_neighbors=8, dims=16, binary quantizer */
  int expected_sizes[] = {1, 64, 16};

  while (size >= 4) {
    int target_row = (fuzz_byte(&data, &size, 0) % 12) + 1;
    int col_idx = fuzz_byte(&data, &size, 0) % 3;
    uint8_t corrupt_mode = fuzz_byte(&data, &size, 0) % 6;
    uint8_t extra = fuzz_byte(&data, &size, 0);

    char sqlbuf[256];
    snprintf(sqlbuf, sizeof(sqlbuf),
      "UPDATE v_diskann_nodes00 SET %s = ? WHERE rowid = ?",
      columns[col_idx]);

    sqlite3_stmt *writeStmt;
    rc = sqlite3_prepare_v2(db, sqlbuf, -1, &writeStmt, NULL);
    if (rc != SQLITE_OK) continue;

    int expected = expected_sizes[col_idx];
    unsigned char *blob = NULL;
    int blob_size = 0;

    switch (corrupt_mode) {
      case 0: {
        /* Truncated blob: 0 to expected-1 bytes */
        blob_size = extra % expected;
        if (blob_size == 0) blob_size = 0;  /* zero-length is interesting */
        blob = sqlite3_malloc(blob_size > 0 ? blob_size : 1);
        if (!blob) { sqlite3_finalize(writeStmt); continue; }
        for (int i = 0; i < blob_size; i++) {
          blob[i] = fuzz_byte(&data, &size, 0);
        }
        break;
      }
      case 1: {
        /* Oversized blob: expected + extra bytes */
        blob_size = expected + (extra % 64);
        blob = sqlite3_malloc(blob_size);
        if (!blob) { sqlite3_finalize(writeStmt); continue; }
        for (int i = 0; i < blob_size; i++) {
          blob[i] = fuzz_byte(&data, &size, 0xFF);
        }
        break;
      }
      case 2: {
        /* Zero-length blob */
        blob_size = 0;
        blob = NULL;
        sqlite3_bind_zeroblob(writeStmt, 1, 0);
        sqlite3_bind_int64(writeStmt, 2, target_row);
        sqlite3_step(writeStmt);
        sqlite3_finalize(writeStmt);
        continue;
      }
      case 3: {
        /* Correct size but all-ones validity (all slots "valid") with
         * garbage neighbor IDs -- forces reading non-existent nodes */
        blob_size = expected;
        blob = sqlite3_malloc(blob_size);
        if (!blob) { sqlite3_finalize(writeStmt); continue; }
        memset(blob, 0xFF, blob_size);
        break;
      }
      case 4: {
        /* neighbor_ids with very large rowid values (near INT64_MAX) */
        blob_size = expected;
        blob = sqlite3_malloc(blob_size);
        if (!blob) { sqlite3_finalize(writeStmt); continue; }
        memset(blob, 0x7F, blob_size); /* fills with large positive values */
        break;
      }
      case 5: {
        /* neighbor_ids with negative rowid values (rowid=0 is sentinel) */
        blob_size = expected;
        blob = sqlite3_malloc(blob_size);
        if (!blob) { sqlite3_finalize(writeStmt); continue; }
        memset(blob, 0x80, blob_size); /* fills with large negative values */
        /* Flip some bytes from fuzz data */
        for (int i = 0; i < blob_size && size > 0; i++) {
          blob[i] ^= fuzz_byte(&data, &size, 0);
        }
        break;
      }
    }

    if (blob) {
      sqlite3_bind_blob(writeStmt, 1, blob, blob_size, SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_blob(writeStmt, 1, "", 0, SQLITE_STATIC);
    }
    sqlite3_bind_int64(writeStmt, 2, target_row);
    sqlite3_step(writeStmt);
    sqlite3_finalize(writeStmt);
    sqlite3_free(blob);
  }

  /* Exercise the corrupted graph with various operations */

  /* KNN query */
  {
    float qvec[16];
    for (int j = 0; j < 16; j++) qvec[j] = (float)j * 0.1f;
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

  /* Insert into corrupted graph (triggers add_reverse_edge on corrupted nodes) */
  {
    float vec[16];
    for (int j = 0; j < 16; j++) vec[j] = 0.5f;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmt, NULL);
    if (stmt) {
      sqlite3_bind_int64(stmt, 1, 100);
      sqlite3_bind_blob(stmt, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }
  }

  /* Delete from corrupted graph (triggers repair_reverse_edges) */
  {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
      "DELETE FROM v WHERE rowid = ?", -1, &stmt, NULL);
    if (stmt) {
      sqlite3_bind_int64(stmt, 1, 5);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }
  }

  /* Another KNN to traverse the post-mutation graph */
  {
    float qvec[16];
    for (int j = 0; j < 16; j++) qvec[j] = -0.5f + (float)j * 0.07f;
    sqlite3_stmt *knnStmt;
    rc = sqlite3_prepare_v2(db,
      "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = 12",
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
