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

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  // Create table with fuzz input as column definitions
  sqlite3_str *s = sqlite3_str_new(NULL);
  assert(s);
  sqlite3_str_appendall(s, "CREATE VIRTUAL TABLE v USING vec0(");
  sqlite3_str_appendf(s, "%.*s", (int)size, data);
  sqlite3_str_appendall(s, ")");
  char *zSql = sqlite3_str_finish(s);
  assert(zSql);

  rc = sqlite3_exec(db, zSql, NULL, NULL, NULL);
  sqlite3_free(zSql);

  if (rc == SQLITE_OK) {
    // Table was created — try to use it. These may fail (errors are fine),
    // but must never crash.
    sqlite3_exec(db, "INSERT INTO v(rowid) VALUES (1)", NULL, NULL, NULL);
    sqlite3_exec(db, "SELECT * FROM v", NULL, NULL, NULL);
    sqlite3_exec(db, "SELECT * FROM v WHERE rowid = 1", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM v WHERE rowid = 1", NULL, NULL, NULL);
    sqlite3_exec(db, "DROP TABLE v", NULL, NULL, NULL);
  }

  sqlite3_close(db);
  return 0;
}
