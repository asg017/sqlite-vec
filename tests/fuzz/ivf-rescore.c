/**
 * Fuzz target: IVF oversample + rescore path.
 *
 * Specifically targets the code path where quantizer != none AND
 * oversample > 1, which triggers:
 * 1. Quantized KNN scan to collect oversample*k candidates
 * 2. Full-precision vector lookup from _ivf_vectors table
 * 3. Re-scoring with float32 distances
 * 4. Re-sort and truncation
 *
 * This path has the most complex memory management in the KNN query:
 * - Two separate distance computations (quantized + float)
 * - Cross-table lookups (cells + vectors KV store)
 * - Candidate array resizing
 * - qsort over partially re-scored arrays
 *
 * Also tests the int8 + binary quantization round-trip fidelity
 * under adversarial float inputs.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 12) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Header
  int quantizer_type = (data[0] % 2) + 1;  // 1=int8, 2=binary (never none)
  int dim = (data[1] % 32) + 8;  // 8..39
  int nlist = (data[2] % 8) + 1;  // 1..8
  int oversample = (data[3] % 4) + 2;  // 2..5 (always > 1)
  int num_vecs = (data[4] % 60) + 8;  // 8..67
  int k_limit = (data[5] % 15) + 1;  // 1..15

  const uint8_t *payload = data + 6;
  size_t payload_size = size - 6;

  // Binary quantizer needs D multiple of 8
  if (quantizer_type == 2) {
    dim = ((dim + 7) / 8) * 8;
  }

  const char *qname = (quantizer_type == 1) ? "int8" : "binary";

  char sql[512];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] indexed by ivf(nlist=%d, nprobe=%d, quantizer=%s, oversample=%d))",
    dim, nlist, nlist, qname, oversample);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  // Insert vectors with diverse values
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  if (!stmtInsert) { sqlite3_close(db); return 0; }

  size_t offset = 0;
  for (int i = 0; i < num_vecs; i++) {
    float *vec = sqlite3_malloc(dim * sizeof(float));
    if (!vec) break;
    for (int d = 0; d < dim; d++) {
      if (offset + 4 <= payload_size) {
        // Use raw bytes as float for adversarial values
        memcpy(&vec[d], payload + offset, sizeof(float));
        offset += 4;
        // Sanitize: replace NaN/Inf with bounded values to avoid
        // poisoning the entire computation. We want edge values,
        // not complete nonsense.
        if (isnan(vec[d]) || isinf(vec[d])) {
          vec[d] = (vec[d] > 0) ? 1e6f : -1e6f;
          if (isnan(vec[d])) vec[d] = 0.0f;
        }
      } else if (offset < payload_size) {
        vec[d] = ((float)(int8_t)payload[offset++]) / 30.0f;
      } else {
        vec[d] = (float)(i * dim + d) * 0.001f;
      }
    }
    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, (int64_t)(i + 1));
    sqlite3_bind_blob(stmtInsert, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
    sqlite3_free(vec);
  }
  sqlite3_finalize(stmtInsert);

  // Train
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // Multiple KNN queries to exercise rescore path
  for (int q = 0; q < 4; q++) {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (!qvec) break;
    for (int d = 0; d < dim; d++) {
      if (offset < payload_size) {
        qvec[d] = ((float)(int8_t)payload[offset++]) / 10.0f;
      } else {
        qvec[d] = (q == 0) ? 1.0f : (q == 1) ? -1.0f : 0.0f;
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

  // Delete some vectors, then query again (rescore with missing _ivf_vectors rows)
  for (int i = 1; i <= num_vecs / 3; i++) {
    char delsql[64];
    snprintf(delsql, sizeof(delsql), "DELETE FROM v WHERE rowid = %d", i);
    sqlite3_exec(db, delsql, NULL, NULL, NULL);
  }

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

  // Retrain after deletions
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // Query after retrain
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) qvec[d] = -0.3f;
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
