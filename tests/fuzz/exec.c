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
  if(size < 1) return 0;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);
  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);

  const char * zSrc = sqlite3_mprintf("%.*s", size, data);
  assert(zSrc);

  sqlite3_exec(db, zSrc, NULL, NULL, NULL);
  sqlite3_free(zSrc);

  sqlite3_close(db);
  return 0;
}
