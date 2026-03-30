/**
 * Fuzz target: IVF shadow table corruption.
 *
 * Creates a trained IVF table, then corrupts IVF shadow table blobs
 * (centroids, cells validity/rowids/vectors, rowid_map) with fuzz data.
 * Then exercises all read/write paths. Must not crash.
 *
 * Targets:
 * - Cell validity bitmap with wrong size
 * - Cell rowids blob with wrong size/alignment
 * - Cell vectors blob with wrong size
 * - Centroid blob with wrong size
 * - n_vectors inconsistent with validity bitmap
 * - Missing rowid_map entries
 * - KNN scan over corrupted cells
 * - Insert/delete with corrupted rowid_map
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
  if (size < 4) return 0;

  int rc;
  sqlite3 *db;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Create IVF table and insert enough vectors to train
  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[8] indexed by ivf(nlist=2, nprobe=2))",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  // Insert 10 vectors
  {
    sqlite3_stmt *si = NULL;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &si, NULL);
    if (!si) { sqlite3_close(db); return 0; }
    for (int i = 0; i < 10; i++) {
      float vec[8];
      for (int d = 0; d < 8; d++) {
        vec[d] = (float)(i * 8 + d) * 0.1f;
      }
      sqlite3_reset(si);
      sqlite3_bind_int64(si, 1, i + 1);
      sqlite3_bind_blob(si, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
      sqlite3_step(si);
    }
    sqlite3_finalize(si);
  }

  // Train
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // Now corrupt shadow tables based on fuzz input
  uint8_t target = data[0] % 10;
  const uint8_t *payload = data + 1;
  int payload_size = (int)(size - 1);

  // Limit payload to avoid huge allocations
  if (payload_size > 4096) payload_size = 4096;

  sqlite3_stmt *stmt = NULL;

  switch (target) {
    case 0: {
      // Corrupt cell validity blob
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_ivf_cells00 SET validity = ? WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt); sqlite3_finalize(stmt);
      }
      break;
    }
    case 1: {
      // Corrupt cell rowids blob
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_ivf_cells00 SET rowids = ? WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt); sqlite3_finalize(stmt);
      }
      break;
    }
    case 2: {
      // Corrupt cell vectors blob
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_ivf_cells00 SET vectors = ? WHERE rowid = 1",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt); sqlite3_finalize(stmt);
      }
      break;
    }
    case 3: {
      // Corrupt centroid blob
      rc = sqlite3_prepare_v2(db,
        "UPDATE v_ivf_centroids00 SET centroid = ? WHERE centroid_id = 0",
        -1, &stmt, NULL);
      if (rc == SQLITE_OK) {
        sqlite3_bind_blob(stmt, 1, payload, payload_size, SQLITE_STATIC);
        sqlite3_step(stmt); sqlite3_finalize(stmt);
      }
      break;
    }
    case 4: {
      // Set n_vectors to a bogus value (larger than cell capacity)
      int bogus_n = 99999;
      if (payload_size >= 4) {
        memcpy(&bogus_n, payload, 4);
        bogus_n = abs(bogus_n) % 100000;
      }
      char sql[128];
      snprintf(sql, sizeof(sql),
        "UPDATE v_ivf_cells00 SET n_vectors = %d WHERE rowid = 1", bogus_n);
      sqlite3_exec(db, sql, NULL, NULL, NULL);
      break;
    }
    case 5: {
      // Delete rowid_map entries (orphan vectors)
      sqlite3_exec(db,
        "DELETE FROM v_ivf_rowid_map00 WHERE rowid IN (1, 2, 3)",
        NULL, NULL, NULL);
      break;
    }
    case 6: {
      // Corrupt rowid_map slot values
      char sql[128];
      int bogus_slot = payload_size > 0 ? (int)payload[0] * 1000 : 99999;
      snprintf(sql, sizeof(sql),
        "UPDATE v_ivf_rowid_map00 SET slot = %d WHERE rowid = 1", bogus_slot);
      sqlite3_exec(db, sql, NULL, NULL, NULL);
      break;
    }
    case 7: {
      // Corrupt rowid_map cell_id values
      sqlite3_exec(db,
        "UPDATE v_ivf_rowid_map00 SET cell_id = 99999 WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
    case 8: {
      // Delete all centroids (make trained but no centroids)
      sqlite3_exec(db,
        "DELETE FROM v_ivf_centroids00",
        NULL, NULL, NULL);
      break;
    }
    case 9: {
      // Set validity to NULL
      sqlite3_exec(db,
        "UPDATE v_ivf_cells00 SET validity = NULL WHERE rowid = 1",
        NULL, NULL, NULL);
      break;
    }
  }

  // Exercise all read paths over corrupted state — must not crash
  float qvec[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

  // KNN query
  {
    sqlite3_stmt *sk = NULL;
    sqlite3_prepare_v2(db,
      "SELECT rowid, distance FROM v WHERE emb MATCH ? LIMIT 5",
      -1, &sk, NULL);
    if (sk) {
      sqlite3_bind_blob(sk, 1, qvec, sizeof(qvec), SQLITE_STATIC);
      while (sqlite3_step(sk) == SQLITE_ROW) {}
      sqlite3_finalize(sk);
    }
  }

  // Full scan
  sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);

  // Point query
  sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 1", NULL, NULL, NULL);
  sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 5", NULL, NULL, NULL);

  // Delete
  sqlite3_exec(db, "DELETE FROM v WHERE rowid = 3", NULL, NULL, NULL);

  // Insert after corruption
  {
    float newvec[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    sqlite3_stmt *si = NULL;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &si, NULL);
    if (si) {
      sqlite3_bind_int64(si, 1, 100);
      sqlite3_bind_blob(si, 2, newvec, sizeof(newvec), SQLITE_STATIC);
      sqlite3_step(si);
      sqlite3_finalize(si);
    }
  }

  // compute-centroids over corrupted state
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('compute-centroids')",
    NULL, NULL, NULL);

  // clear-centroids
  sqlite3_exec(db,
    "INSERT INTO v(rowid) VALUES ('clear-centroids')",
    NULL, NULL, NULL);

  sqlite3_close(db);
  return 0;
}
