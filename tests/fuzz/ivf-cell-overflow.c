/**
 * Fuzz target: IVF cell overflow and boundary conditions.
 *
 * Pushes cells past VEC0_IVF_CELL_MAX_VECTORS (64) to trigger cell
 * splitting, then exercises blob I/O at slot boundaries.
 *
 * Targets:
 * - Cell splitting when n_vectors reaches cap (64)
 * - Blob offset arithmetic: slot * vecSize, slot / 8, slot % 8
 * - Validity bitmap at byte boundaries (slot 7->8, 15->16, etc.)
 * - Insert into full cell -> create new cell path
 * - Delete from various slot positions (first, last, middle)
 * - Multiple cells per centroid
 * - assign-vectors command with multi-cell centroids
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
  if (size < 8) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Use small dimensions for speed but enough vectors to overflow cells
  int dim = (data[0] % 8) + 2;  // 2..9
  int nlist = (data[1] % 4) + 1;  // 1..4
  // We need >64 vectors to overflow a cell
  int num_vecs = (data[2] % 64) + 65;  // 65..128
  int delete_pattern = data[3];  // Controls which vectors to delete

  const uint8_t *payload = data + 4;
  size_t payload_size = size - 4;

  char sql[256];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] indexed by ivf(nlist=%d, nprobe=%d))",
    dim, nlist, nlist);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  // Insert enough vectors to overflow at least one cell
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  if (!stmtInsert) { sqlite3_close(db); return 0; }

  size_t offset = 0;
  for (int i = 0; i < num_vecs; i++) {
    float *vec = sqlite3_malloc(dim * sizeof(float));
    if (!vec) break;
    for (int d = 0; d < dim; d++) {
      if (offset < payload_size) {
        vec[d] = ((float)(int8_t)payload[offset++]) / 50.0f;
      } else {
        // Cluster vectors near specific centroids to ensure some cells overflow
        int cluster = i % nlist;
        vec[d] = (float)cluster + (float)(i % 10) * 0.01f + d * 0.001f;
      }
    }
    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, (int64_t)(i + 1));
    sqlite3_bind_blob(stmtInsert, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
    sqlite3_free(vec);
  }
  sqlite3_finalize(stmtInsert);

  // Train to assign vectors to centroids (triggers cell building)
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // Delete vectors at boundary positions based on fuzz data
  // This tests validity bitmap manipulation at different slot positions
  for (int i = 0; i < num_vecs; i++) {
    int byte_idx = i / 8;
    if (byte_idx < (int)payload_size && (payload[byte_idx] & (1 << (i % 8)))) {
      // Use delete_pattern to thin deletions
      if ((delete_pattern + i) % 3 == 0) {
        char delsql[64];
        snprintf(delsql, sizeof(delsql), "DELETE FROM v WHERE rowid = %d", i + 1);
        sqlite3_exec(db, delsql, NULL, NULL, NULL);
      }
    }
  }

  // Insert more vectors after deletions (into cells with holes)
  {
    sqlite3_stmt *si = NULL;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &si, NULL);
    if (si) {
      for (int i = 0; i < 10; i++) {
        float *vec = sqlite3_malloc(dim * sizeof(float));
        if (!vec) break;
        for (int d = 0; d < dim; d++)
          vec[d] = (float)(i + 200) * 0.01f;
        sqlite3_reset(si);
        sqlite3_bind_int64(si, 1, (int64_t)(num_vecs + i + 1));
        sqlite3_bind_blob(si, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_step(si);
        sqlite3_free(vec);
      }
      sqlite3_finalize(si);
    }
  }

  // KNN query that must scan multiple cells per centroid
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) qvec[d] = 0.0f;
      sqlite3_stmt *sk = NULL;
      snprintf(sql, sizeof(sql),
        "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 20");
      sqlite3_prepare_v2(db, sql, -1, &sk, NULL);
      if (sk) {
        sqlite3_bind_blob(sk, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
        while (sqlite3_step(sk) == SQLITE_ROW) {}
        sqlite3_finalize(sk);
      }
      sqlite3_free(qvec);
    }
  }

  // Test assign-vectors with multi-cell state
  // First clear centroids
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('clear-centroids')",
    NULL, NULL, NULL);

  // Set centroids manually, then assign
  for (int c = 0; c < nlist; c++) {
    float *cvec = sqlite3_malloc(dim * sizeof(float));
    if (!cvec) break;
    for (int d = 0; d < dim; d++) cvec[d] = (float)c + d * 0.1f;

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
      "INSERT INTO v(rowid, emb) VALUES ('set-centroid:%d', ?)", c);
    sqlite3_stmt *sc = NULL;
    sqlite3_prepare_v2(db, cmd, -1, &sc, NULL);
    if (sc) {
      sqlite3_bind_blob(sc, 1, cvec, dim * sizeof(float), SQLITE_TRANSIENT);
      sqlite3_step(sc);
      sqlite3_finalize(sc);
    }
    sqlite3_free(cvec);
  }

  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('assign-vectors')",
    NULL, NULL, NULL);

  // Final query after assign-vectors
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) qvec[d] = 1.0f;
      sqlite3_stmt *sk = NULL;
      sqlite3_prepare_v2(db,
        "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 5",
        -1, &sk, NULL);
      if (sk) {
        sqlite3_bind_blob(sk, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
        while (sqlite3_step(sk) == SQLITE_ROW) {}
        sqlite3_finalize(sk);
      }
      sqlite3_free(qvec);
    }
  }

  // Full scan
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

  sqlite3_close(db);
  return 0;
}
