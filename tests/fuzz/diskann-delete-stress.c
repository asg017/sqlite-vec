/**
 * Fuzz target for DiskANN delete path and graph connectivity maintenance.
 *
 * The delete path is the most complex graph mutation:
 *   1. Read deleted node's neighbor list
 *   2. For each neighbor, remove deleted node from their list
 *   3. Try to fill the gap with one of deleted node's other neighbors
 *   4. Handle medoid deletion (pick new medoid)
 *
 * Edge cases this targets:
 *   - Delete the medoid (entry point) -- forces medoid reassignment
 *   - Delete all nodes except one -- graph degenerates
 *   - Delete nodes in a chain -- cascading dangling edges
 *   - Re-insert at deleted rowids -- stale graph edges to old data
 *   - Delete nonexistent rowids -- should be no-op
 *   - Insert-delete-insert same rowid rapidly
 *   - Delete when graph has exactly n_neighbors entries (full nodes)
 *
 * Key code paths:
 *   - diskann_delete -> diskann_repair_reverse_edges
 *   - diskann_medoid_handle_delete
 *   - diskann_node_clear_neighbor
 *   - Interaction between delete and concurrent search
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 20) return 0;

  int rc;
  sqlite3 *db;
  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  /* int8 quantizer to exercise that distance code path */
  uint8_t quant = fuzz_byte(&data, &size, 0) % 2;
  const char *qname = quant ? "int8" : "binary";

  char sql[256];
  snprintf(sql, sizeof(sql),
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[8] INDEXED BY diskann(neighbor_quantizer=%s, n_neighbors=8))",
    qname);
  rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  sqlite3_stmt *stmtInsert = NULL, *stmtDelete = NULL, *stmtKnn = NULL;
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "DELETE FROM v WHERE rowid = ?", -1, &stmtDelete, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = ?",
    -1, &stmtKnn, NULL);

  if (!stmtInsert || !stmtDelete || !stmtKnn) goto cleanup;

  /* Phase 1: Build a graph of exactly n_neighbors+2 = 10 nodes.
   * This makes every node nearly full, maximizing the chance that
   * inserts trigger the "full node" path in add_reverse_edge. */
  for (int i = 1; i <= 10; i++) {
    float vec[8];
    for (int j = 0; j < 8; j++) {
      vec[j] = (float)((int8_t)fuzz_byte(&data, &size, (uint8_t)(i*13+j*7))) / 20.0f;
    }
    sqlite3_reset(stmtInsert);
    sqlite3_bind_int64(stmtInsert, 1, i);
    sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
    sqlite3_step(stmtInsert);
  }

  /* Phase 2: Fuzz-driven delete-heavy workload */
  while (size >= 2) {
    uint8_t op = fuzz_byte(&data, &size, 0);
    uint8_t param = fuzz_byte(&data, &size, 0);

    switch (op % 6) {
      case 0: /* Delete existing node */
      case 1: { /* (weighted toward deletes) */
        int64_t rowid = (int64_t)(param % 16) + 1;
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);
        break;
      }
      case 2: { /* Delete then immediately re-insert same rowid */
        int64_t rowid = (int64_t)(param % 10) + 1;
        sqlite3_reset(stmtDelete);
        sqlite3_bind_int64(stmtDelete, 1, rowid);
        sqlite3_step(stmtDelete);

        float vec[8];
        for (int j = 0; j < 8; j++) {
          vec[j] = (float)((int8_t)fuzz_byte(&data, &size, (uint8_t)(rowid+j))) / 15.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 3: { /* KNN query on potentially sparse/empty graph */
        float qvec[8];
        for (int j = 0; j < 8; j++) {
          qvec[j] = (float)((int8_t)fuzz_byte(&data, &size, 0)) / 10.0f;
        }
        int k = (param % 15) + 1;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, k);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 4: { /* Insert new node */
        int64_t rowid = (int64_t)(param % 32) + 1;
        float vec[8];
        for (int j = 0; j < 8; j++) {
          vec[j] = (float)((int8_t)fuzz_byte(&data, &size, 0)) / 10.0f;
        }
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, rowid);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
      case 5: { /* Delete ALL remaining nodes, then insert fresh */
        for (int i = 1; i <= 32; i++) {
          sqlite3_reset(stmtDelete);
          sqlite3_bind_int64(stmtDelete, 1, i);
          sqlite3_step(stmtDelete);
        }
        /* Now insert one node into empty graph */
        float vec[8] = {1.0f, 0, 0, 0, 0, 0, 0, 0};
        sqlite3_reset(stmtInsert);
        sqlite3_bind_int64(stmtInsert, 1, 1);
        sqlite3_bind_blob(stmtInsert, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
        sqlite3_step(stmtInsert);
        break;
      }
    }
  }

  /* Final KNN on whatever state the graph is in */
  {
    float qvec[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    sqlite3_reset(stmtKnn);
    sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtKnn, 2, 10);
    while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
  }

cleanup:
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtDelete);
  sqlite3_finalize(stmtKnn);
  sqlite3_close(db);
  return 0;
}
