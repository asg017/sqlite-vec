#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/*
 * Fuzz target that exercises error-path cleanup in vector functions.
 *
 * The key insight: when a vector is parsed from JSON TEXT, the cleanup
 * function is sqlite3_free (heap allocator). When parsed from BLOB,
 * cleanup is a no-op. Bugs in cleanup code (wrong pointer, missing
 * cleanup, double-free) are only observable with the sqlite3_free path.
 *
 * This fuzzer systematically covers:
 *   1. Valid JSON arg1 + invalid fuzz arg2  (parse failure → cleanup arg1)
 *   2. Valid JSON arg1 + valid JSON arg2 with different dimensions
 *      (dimension mismatch → cleanup both)
 *   3. Valid JSON arg1 + int8/bit blob arg2 with mismatched type
 *      (type mismatch → cleanup both)
 *   4. Fuzz arg1 + valid JSON arg2  (parse failure of arg1, no cleanup)
 *   5. Single-arg functions with JSON text  (normal cleanup path)
 *   6. Single-arg functions with fuzz text  (parse failure path)
 */

/* Helper: bind a valid vector to a statement parameter.
 * mode selects the vector type and format. */
static void bind_valid_vector(sqlite3_stmt *stmt, int param, int mode) {
  /* JSON text vectors — cleanup = sqlite3_free */
  static const char *json_f32_4d = "[1.0, 0.0, 0.0, 0.0]";
  static const char *json_f32_2d = "[1.0, 2.0]";
  static const char *json_f32_1d = "[1.0]";

  /* Blob vectors — cleanup = noop */
  static const float blob_f32_4d[] = {1.0f, 0.0f, 0.0f, 0.0f};
  static const float blob_f32_2d[] = {1.0f, 2.0f};

  /* int8 blob — 4 bytes = 4 dimensions */
  static const int8_t blob_int8_4d[] = {10, 20, 30, 40};

  /* bit blob — 1 byte = 8 bits */
  static const uint8_t blob_bit_1b[] = {0xAA};

  switch (mode % 7) {
    case 0: sqlite3_bind_text(stmt, param, json_f32_4d, -1, SQLITE_STATIC); break;
    case 1: sqlite3_bind_text(stmt, param, json_f32_2d, -1, SQLITE_STATIC); break;
    case 2: sqlite3_bind_text(stmt, param, json_f32_1d, -1, SQLITE_STATIC); break;
    case 3: sqlite3_bind_blob(stmt, param, blob_f32_4d, sizeof(blob_f32_4d), SQLITE_STATIC); break;
    case 4: sqlite3_bind_blob(stmt, param, blob_f32_2d, sizeof(blob_f32_2d), SQLITE_STATIC); break;
    case 5: /* int8 — must set subtype */
      sqlite3_bind_blob(stmt, param, blob_int8_4d, sizeof(blob_int8_4d), SQLITE_STATIC);
      break;
    case 6: /* bit — must set subtype */
      sqlite3_bind_blob(stmt, param, blob_bit_1b, sizeof(blob_bit_1b), SQLITE_STATIC);
      break;
  }
}

static void run_query(sqlite3 *db, const char *sql,
                      int arg1_mode, int arg2_mode,
                      const uint8_t *fuzz, int fuzz_len,
                      int fuzz_arg, int fuzz_as_text) {
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) return;

  int nParams = sqlite3_bind_parameter_count(stmt);

  for (int p = 1; p <= nParams; p++) {
    if (p == fuzz_arg) {
      /* Bind fuzz data */
      if (fuzz_as_text)
        sqlite3_bind_text(stmt, p, (const char *)fuzz, fuzz_len, SQLITE_STATIC);
      else
        sqlite3_bind_blob(stmt, p, fuzz, fuzz_len, SQLITE_STATIC);
    } else if (p == 1) {
      bind_valid_vector(stmt, p, arg1_mode);
    } else {
      bind_valid_vector(stmt, p, arg2_mode);
    }
  }

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 3) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* --- Decode fuzz control bytes --- */
  uint8_t b0 = data[0];
  uint8_t b1 = data[1];
  uint8_t b2 = data[2];
  const uint8_t *payload = data + 3;
  int payload_size = (int)(size - 3);

  /* Two-argument vector functions */
  static const char *two_arg[] = {
    "SELECT vec_distance_l2(?, ?)",
    "SELECT vec_distance_cosine(?, ?)",
    "SELECT vec_distance_l1(?, ?)",
    "SELECT vec_distance_hamming(?, ?)",
    "SELECT vec_add(?, ?)",
    "SELECT vec_sub(?, ?)",
  };

  /* Single-argument vector functions that call cleanup */
  static const char *one_arg[] = {
    "SELECT vec_f32(?)",
    "SELECT vec_int8(?)",
    "SELECT vec_bit(?)",
    "SELECT vec_length(?)",
    "SELECT vec_type(?)",
    "SELECT vec_to_json(?)",
    "SELECT vec_normalize(?)",
    "SELECT vec_quantize_binary(?)",
  };

  int qIdx2 = b0 % 6;
  int qIdx1 = b0 % 8;
  int arg1_mode = b1 % 7;
  int arg2_mode = b2 % 7;

  /*
   * Phase 1: Two-arg functions — fuzz arg2, valid arg1
   *   Exercises: parse-failure cleanup of arg1 (the fixed bug),
   *   type mismatch cleanup, dimension mismatch cleanup.
   */
  /* arg2 as fuzz blob */
  run_query(db, two_arg[qIdx2], arg1_mode, 0,
            payload, payload_size, /*fuzz_arg=*/2, /*as_text=*/0);
  /* arg2 as fuzz text */
  run_query(db, two_arg[qIdx2], arg1_mode, 0,
            payload, payload_size, /*fuzz_arg=*/2, /*as_text=*/1);

  /*
   * Phase 2: Two-arg functions — fuzz arg1, valid arg2
   *   Exercises: parse-failure of arg1 (no cleanup needed), and
   *   type/dimension mismatch when arg1 parses to unexpected type.
   */
  run_query(db, two_arg[qIdx2], 0, arg2_mode,
            payload, payload_size, /*fuzz_arg=*/1, /*as_text=*/0);
  run_query(db, two_arg[qIdx2], 0, arg2_mode,
            payload, payload_size, /*fuzz_arg=*/1, /*as_text=*/1);

  /*
   * Phase 3: Two-arg — both valid but deliberately mismatched types/dims.
   *   arg1_mode and arg2_mode often produce different types or dimensions.
   *   Exercises: type mismatch (lines 1035-1042) and dimension mismatch
   *   (lines 1044-1051) with sqlite3_free cleanup on both sides.
   */
  run_query(db, two_arg[qIdx2], arg1_mode, arg2_mode,
            NULL, 0, /*fuzz_arg=*/0, /*as_text=*/0);

  /*
   * Phase 4: Single-arg functions — fuzz as blob and text.
   *   Exercises: parse failure paths in vec_f32, vec_int8, vec_bit, etc.
   *   Also exercises normal cleanup when fuzz data happens to be valid.
   */
  run_query(db, one_arg[qIdx1], 0, 0,
            payload, payload_size, /*fuzz_arg=*/1, /*as_text=*/0);
  run_query(db, one_arg[qIdx1], 0, 0,
            payload, payload_size, /*fuzz_arg=*/1, /*as_text=*/1);

  sqlite3_close(db);
  return 0;
}
