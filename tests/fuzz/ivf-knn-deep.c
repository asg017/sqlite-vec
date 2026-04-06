/**
 * Fuzz target: IVF KNN search deep paths.
 *
 * Exercises the full KNN pipeline with fuzz-controlled:
 * - nprobe values (including > nlist, =1, =nlist)
 * - Query vectors (including adversarial floats)
 * - Mix of trained/untrained state
 * - Oversample + rescore path (quantizer=int8 with oversample>1)
 * - Multiple interleaved KNN queries
 * - Candidate array realloc path (many vectors in probed cells)
 *
 * Targets:
 * - ivf_scan_cells_from_stmt: candidate realloc, distance computation
 * - ivf_query_knn: centroid sorting, nprobe selection
 * - Oversample rescore: re-ranking with full-precision vectors
 * - qsort with NaN distances
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Header
  int dim = (data[0] % 32) + 2;  // 2..33
  int nlist = (data[1] % 16) + 1;  // 1..16
  int nprobe_initial = (data[2] % 20) + 1;  // 1..20 (can be > nlist)
  int quantizer_type = data[3] % 3;  // 0=none, 1=int8, 2=binary
  int oversample = (data[4] % 4) + 1;  // 1..4
  int num_vecs = (data[5] % 80) + 4;  // 4..83
  int num_queries = (data[6] % 8) + 1;  // 1..8
  int k_limit = (data[7] % 20) + 1;  // 1..20

  const uint8_t *payload = data + 8;
  size_t payload_size = size - 8;

  // For binary quantizer, dimension must be multiple of 8
  if (quantizer_type == 2) {
    dim = ((dim + 7) / 8) * 8;
    if (dim == 0) dim = 8;
  }

  const char *qname;
  switch (quantizer_type) {
    case 1: qname = "int8"; break;
    case 2: qname = "binary"; break;
    default: qname = "none"; break;
  }

  // Oversample only valid with quantization
  if (quantizer_type == 0) oversample = 1;

  // Cap nprobe to nlist for CREATE (parser rejects nprobe > nlist)
  int nprobe_create = nprobe_initial <= nlist ? nprobe_initial : nlist;

  char sql[512];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] indexed by ivf(nlist=%d, nprobe=%d, quantizer=%s%s))",
    dim, nlist, nprobe_create, qname,
    oversample > 1 ? ", oversample=2" : "");

  // If that fails (e.g. oversample with none), try without oversample
  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) {
    snprintf(sql, sizeof(sql),
      "CREATE VIRTUAL TABLE v USING vec0("
      "emb float[%d] indexed by ivf(nlist=%d, nprobe=%d, quantizer=%s))",
      dim, nlist, nprobe_create, qname);
    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }
  }

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
      if (offset < payload_size) {
        vec[d] = ((float)(int8_t)payload[offset++]) / 20.0f;
      } else {
        vec[d] = (float)((i * dim + d) % 256 - 128) / 128.0f;
      }
    }
    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, (int64_t)(i + 1));
    sqlite3_bind_blob(stmtInsert, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
    sqlite3_free(vec);
  }
  sqlite3_finalize(stmtInsert);

  // Query BEFORE training (flat scan path)
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) qvec[d] = 0.5f;
      sqlite3_stmt *sk = NULL;
      snprintf(sql, sizeof(sql),
        "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT %d", k_limit);
      sqlite3_prepare_v2(db, sql, -1, &sk, NULL);
      if (sk) {
        sqlite3_bind_blob(sk, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
        while (sqlite3_step(sk) == SQLITE_ROW) {}
        sqlite3_finalize(sk);
      }
      sqlite3_free(qvec);
    }
  }

  // Train
  sqlite3_exec(db,
    "INSERT INTO v(v) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // Change nprobe at runtime (can exceed nlist -- tests clamping in query)
  {
    char cmd[64];
    snprintf(cmd, sizeof(cmd),
      "INSERT INTO v(v) VALUES ('nprobe=%d')", nprobe_initial);
    sqlite3_exec(db, cmd, NULL, NULL, NULL);
  }

  // Multiple KNN queries with different fuzz-derived query vectors
  for (int q = 0; q < num_queries; q++) {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (!qvec) break;
    for (int d = 0; d < dim; d++) {
      if (offset < payload_size) {
        qvec[d] = ((float)(int8_t)payload[offset++]) / 10.0f;
      } else {
        qvec[d] = (q == 0) ? 1.0f : 0.0f;
      }
    }

    sqlite3_stmt *sk = NULL;
    snprintf(sql, sizeof(sql),
      "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT %d", k_limit);
    sqlite3_prepare_v2(db, sql, -1, &sk, NULL);
    if (sk) {
      sqlite3_bind_blob(sk, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
      while (sqlite3_step(sk) == SQLITE_ROW) {}
      sqlite3_finalize(sk);
    }
    sqlite3_free(qvec);
  }

  // Delete half the vectors then query again
  for (int i = 1; i <= num_vecs / 2; i++) {
    char delsql[64];
    snprintf(delsql, sizeof(delsql), "DELETE FROM v WHERE rowid = %d", i);
    sqlite3_exec(db, delsql, NULL, NULL, NULL);
  }

  // Query after mass deletion
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) qvec[d] = -0.5f;
      sqlite3_stmt *sk = NULL;
      snprintf(sql, sizeof(sql),
        "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT %d", k_limit);
      sqlite3_prepare_v2(db, sql, -1, &sk, NULL);
      if (sk) {
        sqlite3_bind_blob(sk, 1, qvec, dim * sizeof(float), SQLITE_TRANSIENT);
        while (sqlite3_step(sk) == SQLITE_ROW) {}
        sqlite3_finalize(sk);
      }
      sqlite3_free(qvec);
    }
  }

  sqlite3_close(db);
  return 0;
}
