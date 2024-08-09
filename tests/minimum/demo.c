#include "sqlite3.h"
#include "sqlite-vec.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

int main(int argc, char *argv[]) {
  int rc = SQLITE_OK;
  sqlite3 *db;
  sqlite3_stmt *stmt;

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);

  rc = sqlite3_vec_init(db, NULL, NULL);
  assert(rc == SQLITE_OK);


  rc = sqlite3_prepare_v2(db, "SELECT sqlite_version(), vec_version(), (select json_group_array(compile_options) from pragma_compile_options)", -1, &stmt, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_step(stmt);
  printf("sqlite_version=%s, vec_version=%s %s\n", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1), sqlite3_column_text(stmt, 2));
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
