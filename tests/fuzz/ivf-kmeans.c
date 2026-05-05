/**
 * Fuzz target: IVF k-means clustering.
 *
 * Builds a table, inserts fuzz-controlled vectors, then runs
 * compute-centroids with fuzz-controlled parameters (nlist, max_iter, seed).
 * Targets:
 * - kmeans with N < k (clamping), N == 1, k == 1
 * - kmeans with duplicate/identical vectors (all distances zero)
 * - kmeans with NaN/Inf vectors
 * - Empty cluster reassignment path (farthest-point heuristic)
 * - Large nlist relative to N
 * - The compute-centroids:{json} command parsing
 * - clear-centroids followed by compute-centroids (round-trip)
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
  if (size < 10) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Parse fuzz header
  // Byte 0-1: dimension (1..128)
  // Byte 2: nlist for CREATE (1..64)
  // Byte 3: nlist override for compute-centroids (0 = use default)
  // Byte 4: max_iter (1..50)
  // Byte 5-8: seed
  // Byte 9: num_vectors (1..64)
  // Remaining: vector float data

  int dim = (data[0] | (data[1] << 8)) % 128 + 1;
  int nlist_create = (data[2] % 64) + 1;
  int nlist_override = data[3] % 65;  // 0 means use table default
  int max_iter = (data[4] % 50) + 1;
  uint32_t seed = (uint32_t)data[5] | ((uint32_t)data[6] << 8) |
                  ((uint32_t)data[7] << 16) | ((uint32_t)data[8] << 24);
  int num_vecs = (data[9] % 64) + 1;

  const uint8_t *payload = data + 10;
  size_t payload_size = size - 10;

  char sql[256];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] indexed by ivf(nlist=%d, nprobe=%d))",
    dim, nlist_create, nlist_create);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  // Insert vectors
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(v, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  if (!stmtInsert) { sqlite3_close(db); return 0; }

  size_t offset = 0;
  for (int i = 0; i < num_vecs; i++) {
    float *vec = sqlite3_malloc(dim * sizeof(float));
    if (!vec) break;

    for (int d = 0; d < dim; d++) {
      if (offset + 4 <= payload_size) {
        memcpy(&vec[d], payload + offset, sizeof(float));
        offset += 4;
      } else if (offset < payload_size) {
        // Scale to interesting range including values > 1, < -1
        vec[d] = ((float)(int8_t)payload[offset++]) / 5.0f;
      } else {
        // Reuse earlier bytes to fill remaining dimensions
        vec[d] = (float)(i * dim + d) * 0.01f;
      }
    }

    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, (int64_t)(i + 1));
    sqlite3_bind_blob(stmtInsert, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
    sqlite3_free(vec);
  }
  sqlite3_finalize(stmtInsert);

  // Exercise compute-centroids with JSON options
  {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
      "INSERT INTO v(rowid) VALUES "
      "('compute-centroids:{\"nlist\":%d,\"max_iterations\":%d,\"seed\":%u}')",
      nlist_override, max_iter, seed);
    sqlite3_exec(db, cmd, NULL, NULL, NULL);
  }

  // KNN query after training
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) {
        qvec[d] = (d < 3) ? 1.0f : 0.0f;
      }
      sqlite3_stmt *stmtKnn = NULL;
      sqlite3_prepare_v2(db,
        "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 5",
        -1, &stmtKnn, NULL);
      if (stmtKnn) {
        sqlite3_bind_blob(stmtKnn, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        sqlite3_finalize(stmtKnn);
      }
      sqlite3_free(qvec);
    }
  }

  // Clear centroids and re-compute to test round-trip
  sqlite3_exec(db,
    "INSERT INTO v(v) VALUES ('clear-centroids')",
    NULL, NULL, NULL);

  // Insert a few more vectors in untrained state
  {
    sqlite3_stmt *si = NULL;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(v, emb) VALUES (?, ?)", -1, &si, NULL);
    if (si) {
      for (int i = 0; i < 3; i++) {
        float *vec = sqlite3_malloc(dim * sizeof(float));
        if (!vec) break;
        for (int d = 0; d < dim; d++) vec[d] = (float)(i + 100) * 0.1f;
        sqlite3_reset(si);
        sqlite3_bind_int64(si, 1, (int64_t)(num_vecs + i + 1));
        sqlite3_bind_blob(si, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_step(si);
        sqlite3_free(vec);
      }
      sqlite3_finalize(si);
    }
  }

  // Re-train
  sqlite3_exec(db,
    "INSERT INTO v(v) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // Delete some rows after training, then query
  sqlite3_exec(db, "DELETE FROM v WHERE rowid = 1", NULL, NULL, NULL);
  sqlite3_exec(db, "DELETE FROM v WHERE rowid = 2", NULL, NULL, NULL);

  // Query after deletes
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) qvec[d] = 0.5f;
      sqlite3_stmt *stmtKnn = NULL;
      sqlite3_prepare_v2(db,
        "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 10",
        -1, &stmtKnn, NULL);
      if (stmtKnn) {
        sqlite3_bind_blob(stmtKnn, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        sqlite3_finalize(stmtKnn);
      }
      sqlite3_free(qvec);
    }
  }

  sqlite3_close(db);
  return 0;
}
