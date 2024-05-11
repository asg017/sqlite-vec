#include "sqlite3.h"
#include "sqlite-vec.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

int main(int argc, char *argv[]) {
  int rc = SQLITE_OK;
  sqlite3 *db;
  sqlite3_stmt *stmt;

  rc = sqlite3_auto_extension((void (*)())sqlite3_vec_init);
  assert(rc == SQLITE_OK);

  rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK);

  rc = sqlite3_prepare_v2(db, "SELECT sqlite_version(), vec_version()", -1, &stmt, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_step(stmt);
  printf("sqlite_version=%s, vec_version=%s\n", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1));
  sqlite3_finalize(stmt);

  static const struct {
    sqlite3_int64 id;
    float vector[4];
  } items[] = {
    {1, {0.1, 0.1, 0.1, 0.1}},
    {2, {0.2, 0.2, 0.2, 0.2}},
    {3, {0.3, 0.3, 0.3, 0.3}},
    {4, {0.4, 0.4, 0.4, 0.4}},
    {5, {0.5, 0.5, 0.5, 0.5}},
  };
  float query[4] = {0.3, 0.3, 0.3, 0.3};


  rc = sqlite3_prepare_v2(db, "CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])", -1, &stmt, NULL);
  assert(rc == SQLITE_OK);
  rc = sqlite3_step(stmt);
  assert(rc == SQLITE_DONE);
  sqlite3_finalize(stmt);

  rc = sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
  assert(rc == SQLITE_OK);
  rc = sqlite3_prepare_v2(db, "INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)", -1, &stmt, NULL);
  assert(rc == SQLITE_OK);
  for (unsigned long i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
    sqlite3_bind_int64(stmt, 1, items[i].id);
    sqlite3_bind_blob(stmt, 2, items[i].vector, sizeof(items[i].vector), SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  rc = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
  assert(rc == SQLITE_OK);

  rc = sqlite3_prepare_v2(db,
    "SELECT "
    "  rowid, "
    "  distance "
    "FROM vec_items "
    "WHERE embedding MATCH ?1 "
    "ORDER BY distance "
    "LIMIT 3 "
  , -1, &stmt, NULL);
  assert(rc == SQLITE_OK);

  sqlite3_bind_blob(stmt, 1, query, sizeof(query), SQLITE_STATIC);

  while(1) {
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) break;
    assert(rc==SQLITE_ROW);
    sqlite3_int64 rowid = sqlite3_column_int64(stmt, 0);
    double distance = sqlite3_column_double(stmt, 1);
    printf("rowid=%lld distance=%f\n", rowid, distance);
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
