#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) return 0;

  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmt = NULL;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  static const char *queries[] = {
    "SELECT vec_f32(cast(? as text))",       /*  0: JSON text -> f32 */
    "SELECT vec_f32(?)",                      /*  1: blob -> f32 */
    "SELECT vec_int8(?)",                     /*  2: blob -> int8 */
    "SELECT vec_bit(?)",                      /*  3: blob -> bit */
    "SELECT vec_length(?)",                   /*  4: vector length */
    "SELECT vec_type(?)",                     /*  5: vector type string */
    "SELECT vec_to_json(?)",                  /*  6: vector -> JSON */
    "SELECT vec_normalize(?)",                /*  7: normalize */
    "SELECT vec_quantize_binary(?)",          /*  8: quantize to binary */
    "SELECT vec_quantize_int8(?, 'unit')",    /*  9: quantize to int8 */
    "SELECT vec_distance_l2(?, ?)",           /* 10: L2 distance */
    "SELECT vec_distance_cosine(?, ?)",       /* 11: cosine distance */
    "SELECT vec_distance_l1(?, ?)",           /* 12: L1 distance */
    "SELECT vec_distance_hamming(?, ?)",      /* 13: hamming distance */
    "SELECT vec_add(?, ?)",                   /* 14: vector add */
    "SELECT vec_sub(?, ?)",                   /* 15: vector subtract */
    "SELECT vec_slice(?, 0, ?)",              /* 16: vector slice */
  };
  static const int nQueries = sizeof(queries) / sizeof(queries[0]);

  int qIdx = data[0] % nQueries;
  const uint8_t *payload = data + 1;
  int payload_size = (int)(size - 1);

  rc = sqlite3_prepare_v2(db, queries[qIdx], -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    return 0;
  }

  int nParams = sqlite3_bind_parameter_count(stmt);

  // Bind param 1: fuzz payload as blob
  sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);

  if (nParams >= 2) {
    if (qIdx == 16) {
      // vec_slice 3rd param is integer (end index)
      int end_idx = (payload_size > 0) ? (payload[0] % 64) : 0;
      sqlite3_bind_int(stmt, 2, end_idx);
    } else {
      // For 2-param functions (distance, add, sub): split payload in half
      int half = payload_size / 2;
      sqlite3_bind_blob(stmt, 2, payload + half,
                        payload_size - half, SQLITE_STATIC);
    }
  }

  if (nParams >= 3) {
    // vec_slice: param 3 is the end index
    int end_idx = (payload_size > 1) ? (payload[1] % 64) : 0;
    sqlite3_bind_int(stmt, 3, end_idx);
  }

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
