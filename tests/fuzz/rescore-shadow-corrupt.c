#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/**
 * Fuzz target: corrupt rescore shadow tables then exercise KNN/read/write.
 *
 * This targets the dangerous code paths in rescore_knn (Phase 1 + 2):
 * - sqlite3_blob_read into baseVectors with potentially wrong-sized blobs
 * - distance computation on corrupted/partial quantized data
 * - blob_reopen on _rescore_vectors with missing/corrupted rows
 * - insert/delete after corruption (blob_write to wrong offsets)
 *
 * The existing shadow-corrupt.c only tests vec0 without rescore.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 4) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* Pick quantizer type from first byte */
  int use_int8 = data[0] & 1;
  int target = (data[1] % 8);
  const uint8_t *payload = data + 2;
  int payload_size = (int)(size - 2);

  const char *create_sql = use_int8
    ? "CREATE VIRTUAL TABLE v USING vec0("
      "emb float[16] indexed by rescore(quantizer=int8))"
    : "CREATE VIRTUAL TABLE v USING vec0("
      "emb float[16] indexed by rescore(quantizer=bit))";

  rc = sqlite3_exec(db, create_sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  /* Insert several vectors so there's a full chunk to corrupt */
  {
    sqlite3_stmt *ins = NULL;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &ins, NULL);
    if (!ins) { sqlite3_close(db); return 0; }

    for (int i = 1; i <= 8; i++) {
      float vec[16];
      for (int j = 0; j < 16; j++) vec[j] = (float)(i * 10 + j) / 100.0f;
      sqlite3_reset(ins);
      sqlite3_bind_int64(ins, 1, i);
      sqlite3_bind_blob(ins, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
      sqlite3_step(ins);
    }
    sqlite3_finalize(ins);
  }

  /* Now corrupt rescore shadow tables based on fuzz input */
  sqlite3_stmt *stmt = NULL;

  switch (target) {
    case 0: {
      /* Corrupt _rescore_chunks00 vectors blob with fuzz data */
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_rescore_chunks00 SET vectors = ? WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
      }
      break;
    }
    case 1: {
      /* Corrupt _rescore_vectors00 vector blob for a specific row */
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_rescore_vectors00 SET vector = ? WHERE rowid = 3",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
      }
      break;
    }
    case 2: {
      /* Truncate the quantized chunk blob to wrong size */
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_rescore_chunks00 SET vectors = X'DEADBEEF' WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
      }
      break;
    }
    case 3: {
      /* Delete rows from _rescore_vectors (orphan the float vectors) */
      sqlite3_exec(db,
        "DELETE FROM v_rescore_vectors00 WHERE rowid IN (2, 4, 6)",
        NULL, NULL, NULL);
      break;
    }
    case 4: {
      /* Delete the chunk row entirely from _rescore_chunks */
      sqlite3_exec(db,
        "DELETE FROM v_rescore_chunks00 WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
    case 5: {
      /* Set vectors to NULL in _rescore_chunks */
      sqlite3_exec(db,
        "UPDATE v_rescore_chunks00 SET vectors = NULL WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
    case 6: {
      /* Set vector to NULL in _rescore_vectors */
      sqlite3_exec(db,
        "UPDATE v_rescore_vectors00 SET vector = NULL WHERE rowid = 3",
        NULL, NULL, NULL);
      break;
    }
    case 7: {
      /* Corrupt BOTH tables with fuzz data */
      int half = payload_size / 2;
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_rescore_chunks00 SET vectors = ? WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, half, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
      }
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_rescore_vectors00 SET vector = ? WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload + half,
                          payload_size - half, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
      }
      break;
    }
  }

  /* Exercise ALL read/write paths -- NONE should crash */

  /* KNN query (triggers rescore_knn Phase 1 + Phase 2) */
  {
    float qvec[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    sqlite3_stmt *knn = NULL;
    sqlite3_prepare_v2(db,
      "SELECT rowid, distance FROM v WHERE emb MATCH ? "
      "ORDER BY distance LIMIT 5", -1, &knn, NULL);
    if (knn) {
      sqlite3_bind_blob(knn, 1, qvec, sizeof(qvec), SQLITE_STATIC);
      while (sqlite3_step(knn) == SQLITE_ROW) {}
      sqlite3_finalize(knn);
    }
  }

  /* Full scan (triggers reading from _rescore_vectors) */
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

  /* Point lookups */
  sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 1", NULL, NULL, NULL);
  sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 3", NULL, NULL, NULL);

  /* Insert after corruption */
  {
    float vec[16] = {0};
    sqlite3_stmt *ins = NULL;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (99, ?)", -1, &ins, NULL);
    if (ins) {
      sqlite3_bind_blob(ins, 1, vec, sizeof(vec), SQLITE_STATIC);
      sqlite3_step(ins);
      sqlite3_finalize(ins);
    }
  }

  /* Delete after corruption */
  sqlite3_exec(db, "DELETE FROM v WHERE rowid = 5", NULL, NULL, NULL);

  /* Update after corruption */
  {
    float vec[16] = {1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1};
    sqlite3_stmt *upd = NULL;
    sqlite3_prepare_v2(db,
      "UPDATE v SET emb = ? WHERE rowid = 1", -1, &upd, NULL);
    if (upd) {
      sqlite3_bind_blob(upd, 1, vec, sizeof(vec), SQLITE_STATIC);
      sqlite3_step(upd);
      sqlite3_finalize(upd);
    }
  }

  /* KNN again after modifications to corrupted state */
  {
    float qvec[16] = {0,0,0,0, 0,0,0,0, 1,1,1,1, 1,1,1,1};
    sqlite3_stmt *knn = NULL;
    sqlite3_prepare_v2(db,
      "SELECT rowid, distance FROM v WHERE emb MATCH ? "
      "ORDER BY distance LIMIT 3", -1, &knn, NULL);
    if (knn) {
      sqlite3_bind_blob(knn, 1, qvec, sizeof(qvec), SQLITE_STATIC);
      while (sqlite3_step(knn) == SQLITE_ROW) {}
      sqlite3_finalize(knn);
    }
  }

  sqlite3_exec(db, "DROP TABLE v", NULL, NULL, NULL);
  sqlite3_close(db);
  return 0;
}
