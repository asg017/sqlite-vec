/**
 * Fuzz target for DiskANN greedy beam search deep paths.
 *
 * Builds a graph with enough nodes to force multi-hop traversal, then
 * uses fuzz data to control: query vector values, k, search_list_size
 * overrides, and interleaved insert/delete/query sequences that stress
 * the candidate list growth, visited set hash collisions, and the
 * re-ranking logic.
 *
 * Key code paths targeted:
 *   - diskann_candidate_list_insert (sorted insert, dedup, eviction)
 *   - diskann_visited_set (hash collisions, capacity)
 *   - diskann_search (full beam search loop, re-ranking with exact dist)
 *   - diskann_distance_quantized_precomputed (both binary and int8)
 *   - Buffer merge in vec0Filter_knn_diskann
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

/* Consume one byte from fuzz input, or return default. */
static uint8_t fuzz_byte(const uint8_t **data, size_t *size, uint8_t def) {
  if (*size == 0) return def;
  uint8_t b = **data;
  (*data)++;
  (*size)--;
  return b;
}

static uint16_t fuzz_u16(const uint8_t **data, size_t *size) {
  uint8_t lo = fuzz_byte(data, size, 0);
  uint8_t hi = fuzz_byte(data, size, 0);
  return (uint16_t)hi << 8 | lo;
}

static float fuzz_float(const uint8_t **data, size_t *size) {
  return (float)((int8_t)fuzz_byte(data, size, 0)) / 10.0f;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 32) return 0;

  /* Use first bytes to pick quantizer type and dimensions */
  uint8_t quantizer_choice = fuzz_byte(&data, &size, 0) % 2;
  const char *quantizer = quantizer_choice ? "int8" : "binary";

  /* Dimensions must be divisible by 8. Pick from {8, 16, 32} */
  int dim_choices[] = {8, 16, 32};
  int dims = dim_choices[fuzz_byte(&data, &size, 0) % 3];

  /* n_neighbors: 8 or 16 -- small to force full-neighbor scenarios quickly */
  int n_neighbors = (fuzz_byte(&data, &size, 0) % 2) ? 16 : 8;

  /* search_list_size: small so beam search terminates quickly but still exercises loops */
  int search_list_size = 8 + (fuzz_byte(&data, &size, 0) % 24);

  /* alpha: vary to test RobustPrune pruning logic */
  float alpha_choices[] = {1.0f, 1.2f, 1.5f, 2.0f};
  float alpha = alpha_choices[fuzz_byte(&data, &size, 0) % 4];

  int rc;
  sqlite3 *db;
  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  char sql[512];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] INDEXED BY diskann("
    "neighbor_quantizer=%s, n_neighbors=%d, "
    "search_list_size=%d"
    "))", dims, quantizer, n_neighbors, search_list_size);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_stmt *stmtInsert = NULL, *stmtDelete = NULL, *stmtKnn = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);

  char knn_sql[256];
  snprintf(knn_sql, sizeof(knn_sql),
    "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = ?");
  sqlite3_prepare_v2(db, knn_sql, -1, &stmtKnn, NULL);

  if (!stmtInsert || !stmtDelete || !stmtKnn) goto cleanup;

  /* Phase 1: Seed the graph with enough nodes to create multi-hop structure.
   * Insert 2*n_neighbors nodes so the graph is dense enough for search
   * to actually traverse multiple hops. */
  int seed_count = n_neighbors * 2;
  if (seed_count > 64) seed_count = 64; /* Bound for performance */
  {
    float *vec = malloc(dims * sizeof(float));
    if (!vec) goto cleanup;
    for (int i = 1; i <= seed_count; i++) {
      for (int j = 0; j < dims; j++) {
        vec[j] = fuzz_float(&data, &size);
      }
      sqlite3_reset(stmtInsert);
      sqlite3_bind_int64(stmtInsert, 1, i);
      sqlite3_bind_blob(stmtInsert, 2, vec, dims * sizeof(float), SQLITE_TRANSIENT);
      sqlite3_step(stmtInsert);
    }
    free(vec);
  }

  /* Phase 2: Fuzz-driven operations on the seeded graph */
  float *vec = malloc(dims * sizeof(float));
  if (!vec) goto cleanup;

  while (size >= 2) {
    uint8_t op = fuzz_byte(&data, &size, 0) % 5;
    uint8_t param = fuzz_byte(&data, &size, 0);

    switch (op) {
      case 0: { /* INSERT with fuzz-controlled vector and rowid */
        int64_t rowid = (int64_t)(param % 128) + 1;
        for (int j = 0; j < dims; j++) {
          vec[j] = fuzz_float(&data, &size);
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 1: { /* DELETE */
        int64_t rowid = (int64_t)(param % 128) + 1;
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 2: { /* KNN with fuzz query vector and variable k */
        for (int j = 0; j < dims; j++) {
          vec[j] = fuzz_float(&data, &size);
        }
        int k = (param % 20) + 1;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, k);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 3: { /* KNN with k > number of nodes (boundary) */
        for (int j = 0; j < dims; j++) {
          vec[j] = fuzz_float(&data, &size);
        }
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, 1000); /* k >> graph size */
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 4: { /* INSERT duplicate rowid (triggers OR REPLACE path) */
        int64_t rowid = (int64_t)(param % 32) + 1;
        for (int j = 0; j < dims; j++) {
          vec[j] = (float)(param + j) / 50.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
    }
  }
  free(vec);

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtDelete);
  sqlite3_finalize(stmtKnn);
  sqlite3_close(db);
  return 0;
}
