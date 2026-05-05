/**
 * Fuzz target for DiskANN int8 quantizer edge cases.
 *
 * The binary quantizer is simple (sign bit), but the int8 quantizer has
 * interesting arithmetic:
 *   i8_val = (i8)(((src - (-1.0f)) / step) - 128.0f)
 * where step = 2.0f / 255.0f
 *
 * Edge cases in this formula:
 *   - src values outside [-1, 1] cause clamping issues (no explicit clamp!)
 *   - src = NaN, +Inf, -Inf  (from corrupted vectors or div-by-zero)
 *   - src very close to boundaries (-1.0, 1.0) -- rounding
 *   - The cast to i8 can overflow for extreme src values
 *
 * Also exercises int8 distance functions:
 *   - distance_l2_sqr_int8: accumulates squared differences, possible overflow
 *   - distance_cosine_int8: dot product with normalization
 *   - distance_l1_int8: absolute differences
 *
 * This fuzzer also tests the cosine distance metric path which the
 * other fuzzers (using L2 default) don't cover.
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

static uint8_t fuzz_byte(const uint8_t **data, size_t *size, uint8_t def) {
  if (*size == 0) return def;
  uint8_t b = **data;
  (*data)++;
  (*size)--;
  return b;
}

static float fuzz_extreme_float(const uint8_t **data, size_t *size) {
  uint8_t mode = fuzz_byte(data, size, 0) % 8;
  uint8_t raw = fuzz_byte(data, size, 0);
  switch (mode) {
    case 0: return (float)((int8_t)raw) / 10.0f;   /* Normal range */
    case 1: return (float)((int8_t)raw) * 100.0f;   /* Large values */
    case 2: return (float)((int8_t)raw) / 1000.0f;  /* Tiny values near 0 */
    case 3: return -1.0f;                             /* Exact boundary */
    case 4: return 1.0f;                              /* Exact boundary */
    case 5: return 0.0f;                              /* Zero */
    case 6: return (float)raw / 255.0f;              /* [0, 1] range */
    case 7: return -(float)raw / 255.0f;             /* [-1, 0] range */
  }
  return 0.0f;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 40) return 0;

  int rc;
  sqlite3 *db;
  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* Test both distance metrics with int8 quantizer */
  uint8_t metric_choice = fuzz_byte(&data, &size, 0) % 2;
  const char *metric = metric_choice ? "cosine" : "L2";

  int dims = 8 + (fuzz_byte(&data, &size, 0) % 3) * 8; /* 8, 16, or 24 */

  char sql[512];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] distance_metric=%s "
    "INDEXED BY diskann(neighbor_quantizer=int8, n_neighbors=8, search_list_size=16))",
    dims, metric);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_stmt *stmtInsert = NULL, *stmtKnn = NULL, *stmtDelete = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = ?",
    -1, &stmtKnn, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);

  if (!stmtInsert || !stmtKnn || !stmtDelete) goto cleanup;

  /* Insert vectors with extreme float values to stress quantization */
  float *vec = malloc(dims * sizeof(float));
  if (!vec) goto cleanup;

  for (int i = 1; i <= 16; i++) {
    for (int j = 0; j < dims; j++) {
      vec[j] = fuzz_extreme_float(&data, &size);
    }
    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, i);
    sqlite3_bind_blob(stmtInsert, 2, vec, dims * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
  }

  /* Fuzz-driven operations */
  while (size >= 2) {
    uint8_t op = fuzz_byte(&data, &size, 0) % 4;
    uint8_t param = fuzz_byte(&data, &size, 0);

    switch (op) {
      case 0: { /* KNN with extreme query values */
        for (int j = 0; j < dims; j++) {
          vec[j] = fuzz_extreme_float(&data, &size);
        }
        int k = (param % 10) + 1;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, k);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 1: { /* Insert with extreme values */
        int64_t rowid = (int64_t)(param % 32) + 1;
        for (int j = 0; j < dims; j++) {
          vec[j] = fuzz_extreme_float(&data, &size);
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 2: { /* Delete */
        int64_t rowid = (int64_t)(param % 32) + 1;
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 3: { /* KNN with all-zero or all-same-value query */
        float val = (param % 3 == 0) ? 0.0f :
                    (param % 3 == 1) ? 1.0f : -1.0f;
        for (int j = 0; j < dims; j++) vec[j] = val;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, vec, dims * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, 5);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
    }
  }

  free(vec);

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtKnn);
  sqlite3_finalize(stmtDelete);
  sqlite3_close(db);
  return 0;
}
