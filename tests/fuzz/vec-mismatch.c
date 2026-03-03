#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/*
 * Fuzz target for two-argument vector functions (vec_distance_*, vec_add,
 * vec_sub) where the first argument is always a valid vector and the second
 * is fuzz-derived. This exercises the ensure_vector_match() error paths
 * where the first vector parses successfully but the second does not.
 *
 * Critical coverage: when arg1 is TEXT (JSON-parsed), the cleanup function
 * is sqlite3_free rather than a no-op, so cleanup bugs become observable.
 *
 * The first byte selects the function. The remaining bytes form arg 2.
 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) return 0;

  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmt = NULL;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* Two-argument vector functions */
  static const char *queries[] = {
    "SELECT vec_distance_l2(?, ?)",      /* 0 */
    "SELECT vec_distance_cosine(?, ?)",  /* 1 */
    "SELECT vec_distance_l1(?, ?)",      /* 2 */
    "SELECT vec_distance_hamming(?, ?)", /* 3 */
    "SELECT vec_add(?, ?)",              /* 4 */
    "SELECT vec_sub(?, ?)",              /* 5 */
  };
  static const int nQueries = sizeof(queries) / sizeof(queries[0]);

  /* Valid JSON vectors (TEXT) — parsed via fvec_from_value text path,
   * which sets cleanup = sqlite3_free */
  static const char *json_vecs[] = {
    "[1.0, 0.0, 0.0, 0.0]",  /* 4d */
    "[1.0, 2.0]",             /* 2d */
    "[1.0]",                  /* 1d */
  };
  static const int nJsonVecs = sizeof(json_vecs) / sizeof(json_vecs[0]);

  /* Valid blob vectors (BLOB) — parsed via fvec_from_value blob path,
   * which sets cleanup = fvec_cleanup_noop */
  static const float blob_vec[] = {1.0f, 0.0f, 0.0f, 0.0f};

  uint8_t selector = data[0];
  int qIdx = selector % nQueries;
  /* Bits 3-4: select which valid vector and format for arg1 */
  int arg1_mode = (selector / nQueries) % 4;

  const uint8_t *payload = data + 1;
  int payload_size = (int)(size - 1);

  /* --- Test 1: valid arg1, fuzz arg2 --- */
  rc = sqlite3_prepare_v2(db, queries[qIdx], -1, &stmt, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  /* Bind arg1 as either JSON text or blob */
  switch (arg1_mode) {
    case 0: /* JSON text — triggers sqlite3_free cleanup */
      sqlite3_bind_text(stmt, 1, json_vecs[0], -1, SQLITE_STATIC);
      break;
    case 1:
      sqlite3_bind_text(stmt, 1, json_vecs[1], -1, SQLITE_STATIC);
      break;
    case 2:
      sqlite3_bind_text(stmt, 1, json_vecs[2], -1, SQLITE_STATIC);
      break;
    case 3: /* blob — triggers noop cleanup */
      sqlite3_bind_blob(stmt, 1, blob_vec, sizeof(blob_vec), SQLITE_STATIC);
      break;
  }

  /* Bind arg2 as fuzz blob (most likely to fail parsing for non-4-aligned sizes) */
  sqlite3_bind_blob(stmt, 2, payload, payload_size, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;

  /* --- Test 2: same but arg2 as fuzz text --- */
  rc = sqlite3_prepare_v2(db, queries[qIdx], -1, &stmt, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  switch (arg1_mode) {
    case 0:
      sqlite3_bind_text(stmt, 1, json_vecs[0], -1, SQLITE_STATIC);
      break;
    case 1:
      sqlite3_bind_text(stmt, 1, json_vecs[1], -1, SQLITE_STATIC);
      break;
    case 2:
      sqlite3_bind_text(stmt, 1, json_vecs[2], -1, SQLITE_STATIC);
      break;
    case 3:
      sqlite3_bind_blob(stmt, 1, blob_vec, sizeof(blob_vec), SQLITE_STATIC);
      break;
  }

  sqlite3_bind_text(stmt, 2, (const char *)payload, payload_size, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  stmt = NULL;

  /* --- Test 3: fuzz arg1, valid arg2 --- */
  rc = sqlite3_prepare_v2(db, queries[qIdx], -1, &stmt, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, json_vecs[0], -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_close(db);
  return 0;
}
