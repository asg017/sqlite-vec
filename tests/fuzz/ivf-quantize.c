/**
 * Fuzz target: IVF quantization functions.
 *
 * Directly exercises ivf_quantize_int8 and ivf_quantize_binary with
 * fuzz-controlled dimensions and float data. Targets:
 * - ivf_quantize_int8: clamping, int8 overflow boundary
 * - ivf_quantize_binary: D not divisible by 8, memset(D/8) undercount
 * - Round-trip through CREATE TABLE + INSERT with quantized IVF
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
  if (size < 8) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Byte 0: quantizer type (0=int8, 1=binary)
  // Byte 1: dimension (1..64, but we test edge cases)
  // Byte 2: nlist (1..8)
  // Byte 3: num_vectors to insert (1..32)
  // Remaining: float data
  int qtype = data[0] % 2;
  int dim = (data[1] % 64) + 1;
  int nlist = (data[2] % 8) + 1;
  int num_vecs = (data[3] % 32) + 1;
  const uint8_t *payload = data + 4;
  size_t payload_size = size - 4;

  // For binary quantizer, D must be multiple of 8 to avoid the D/8 bug
  // in production. But we explicitly want to test non-multiples too to
  // find the bug. Use dim as-is.
  const char *quantizer = qtype ? "binary" : "int8";

  // Binary quantizer needs D multiple of 8 in current code, but let's
  // test both valid and invalid dimensions to see what happens.
  // For binary with non-multiple-of-8, the code does memset(dst, 0, D/8)
  // which underallocates when D%8 != 0.
  char sql[256];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[%d] indexed by ivf(nlist=%d, nprobe=%d, quantizer=%s))",
    dim, nlist, nlist, quantizer);

  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  // Insert vectors with fuzz-controlled float values
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(v, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  if (!stmtInsert) { sqlite3_close(db); return 0; }

  size_t offset = 0;
  for (int i = 0; i < num_vecs && offset < payload_size; i++) {
    // Build float vector from fuzz data
    float *vec = sqlite3_malloc(dim * sizeof(float));
    if (!vec) break;

    for (int d = 0; d < dim; d++) {
      if (offset + 4 <= payload_size) {
        // Use raw bytes as float -- can produce NaN, Inf, denormals
        memcpy(&vec[d], payload + offset, sizeof(float));
        offset += 4;
      } else if (offset < payload_size) {
        // Partial: use byte as scaled value
        vec[d] = ((float)(int8_t)payload[offset++]) / 50.0f;
      } else {
        vec[d] = 0.0f;
      }
    }

    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, (int64_t)(i + 1));
    sqlite3_bind_blob(stmtInsert, 2, vec, dim * sizeof(float), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
    sqlite3_free(vec);
  }
  sqlite3_finalize(stmtInsert);

  // Trigger compute-centroids to exercise kmeans + quantization together
  sqlite3_exec(db,
    "INSERT INTO v(v) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // KNN query with fuzz-derived query vector
  {
    float *qvec = sqlite3_malloc(dim * sizeof(float));
    if (qvec) {
      for (int d = 0; d < dim; d++) {
        if (offset < payload_size) {
          qvec[d] = ((float)(int8_t)payload[offset++]) / 10.0f;
        } else {
          qvec[d] = 1.0f;
        }
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

  // Full scan
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

  sqlite3_close(db);
  return 0;
}
