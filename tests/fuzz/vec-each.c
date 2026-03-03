#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int rc;
  sqlite3 *db;
  sqlite3_stmt *stmt = NULL;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_prepare_v2(db,
    "SELECT * FROM vec_each(?)", -1, &stmt, NULL);
  assert(rc == SQLITE_OK);

  sqlite3_bind_blob(stmt, 1, data, (int)size, SQLITE_STATIC);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    // Consume all rows — just exercise the iteration path
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
