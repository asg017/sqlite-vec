#include <stdint.h>
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int rc = SQLITE_OK;
  sqlite3 *db;
  sqlite3_stmt *stmt;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);


  rc = sqlite3_prepare_v2(db, "select * from vec_npy_each(?)", -1, &stmt, NULL);
  assert(rc == SQLITE_OK);
  sqlite3_bind_blob(stmt, 1, data, size, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  if(rc != SQLITE_DONE || rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return -1;
  }

  while(1) {
    if(rc == SQLITE_DONE) break;
    if(rc == SQLITE_ROW) continue;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
