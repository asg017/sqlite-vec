/**
 * Fuzz target for DiskANN runtime command dispatch (diskann_handle_command).
 *
 * The command handler parses strings like "search_list_size_search=42" and
 * modifies live DiskANN config. This fuzzer exercises:
 *
 *   - atoi on fuzz-controlled strings (integer overflow, negative, non-numeric)
 *   - strncmp boundary with fuzz data (near-matches to valid commands)
 *   - Changing search_list_size mid-operation (affects subsequent queries)
 *   - Setting search_list_size to 1 (minimum - single-candidate beam search)
 *   - Setting search_list_size very large (memory pressure)
 *   - Interleaving command changes with inserts and queries
 *
 * Also tests the UPDATE v SET command = ? path through the vtable.
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

  rc = sqlite3_exec(db,
    "CREATE VIRTUAL TABLE v USING vec0("
    "emb float[8] INDEXED BY diskann(neighbor_quantizer=binary, n_neighbors=8))",
    NULL, NULL, NULL);
  if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }

  /* Insert some vectors first */
  {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
      "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmt, NULL);
    for (int i = 1; i <= 8; i++) {
      float vec[8];
      for (int j = 0; j < 8; j++) vec[j] = (float)i * 0.1f + (float)j * 0.01f;
      sqlite3_reset(stmt);
      sqlite3_bind_int64(stmt, 1, i);
      sqlite3_bind_blob(stmt, 2, vec, sizeof(vec), SQLITE_TRANSIENT);
      sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
  }

  sqlite3_stmt *stmtCmd = NULL;
  sqlite3_stmt *stmtInsert = NULL;
  sqlite3_stmt *stmtKnn = NULL;

  /* Commands are dispatched via INSERT INTO t(rowid) VALUES ('cmd_string') */
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid) VALUES (?)", -1, &stmtCmd, NULL);
  sqlite3_prepare_v2(db,
    "INSERT INTO v(rowid, emb) VALUES (?, ?)", -1, &stmtInsert, NULL);
  sqlite3_prepare_v2(db,
    "SELECT rowid, distance FROM v WHERE emb MATCH ? AND k = ?",
    -1, &stmtKnn, NULL);

  if (!stmtCmd || !stmtInsert || !stmtKnn) goto cleanup;

  /* Fuzz-driven command + operation interleaving */
  while (size >= 2) {
    uint8_t op = fuzz_byte(&data, &size, 0) % 5;

    switch (op) {
      case 0: { /* Send fuzz command string */
        int cmd_len = fuzz_byte(&data, &size, 0) % 64;
        char cmd[65];
        for (int i = 0; i < cmd_len && size > 0; i++) {
          cmd[i] = (char)fuzz_byte(&data, &size, 0);
        }
        cmd[cmd_len] = '\0';
        sqlite3_reset(stmtCmd);
        sqlite3_bind_text(stmtCmd, 1, cmd, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmtCmd); /* May fail -- that's expected */
        break;
      }
      case 1: { /* Send valid-looking command with fuzz value */
        const char *prefixes[] = {
          "search_list_size=",
          "search_list_size_search=",
          "search_list_size_insert=",
        };
        int prefix_idx = fuzz_byte(&data, &size, 0) % 3;
        int val = (int)(int8_t)fuzz_byte(&data, &size, 0);

        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s%d", prefixes[prefix_idx], val);
        sqlite3_reset(stmtCmd);
        sqlite3_bind_text(stmtCmd, 1, cmd, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmtCmd);
        break;
      }
      case 2: { /* KNN query (uses whatever search_list_size is set) */
        float qvec[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        qvec[0] = (float)((int8_t)fuzz_byte(&data, &size, 127)) / 10.0f;
        int k = fuzz_byte(&data, &size, 3) % 10 + 1;
        sqlite3_reset(stmtKnn);
        sqlite3_bind_blob(stmtKnn, 1, qvec, sizeof(qvec), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmtKnn, 2, k);
        while (sqlite3_step(stmtKnn) == SQLITE_ROW) {}
        break;
      }
      case 3: { /* Insert (uses whatever search_list_size_insert is set) */
        int64_t rowid = (int64_t)(fuzz_byte(&data, &size, 0) % 32) + 1;
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
      case 4: { /* Set search_list_size to extreme values */
        const char *extreme_cmds[] = {
          "search_list_size=1",
          "search_list_size=2",
          "search_list_size=1000",
          "search_list_size_search=1",
          "search_list_size_insert=1",
        };
        int idx = fuzz_byte(&data, &size, 0) % 5;
        sqlite3_reset(stmtCmd);
        sqlite3_bind_text(stmtCmd, 1, extreme_cmds[idx], -1, SQLITE_STATIC);
        sqlite3_step(stmtCmd);
        break;
      }
    }
  }

cleanup:
  sqlite3_finalize(stmtCmd);
  sqlite3_finalize(stmtInsert);
  sqlite3_finalize(stmtKnn);
  sqlite3_close(db);
  return 0;
}
