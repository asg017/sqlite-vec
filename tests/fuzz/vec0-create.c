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

  sqlite3_str * s = sqlite3_str_new(NULL);
  assert(s);
  sqlite3_str_appendall(s, "CREATE VIRTUAL TABLE v USING vec0(");
  sqlite3_str_appendf(s, "%.*s", size, data);
  sqlite3_str_appendall(s, ")");
  const char * zSql = sqlite3_str_finish(s);
  assert(zSql);

  rc = sqlite3_prepare_v2(db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if(rc == SQLITE_OK) {
    sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
