#include "sqlite3.h"
#include "sqlite-vec.h"
#include <stdio.h>
int core_init(const char *dummy) {
  int rc = sqlite3_auto_extension((void *)sqlite3_vec_init);
  if(rc != SQLITE_OK) return rc;
  return sqlite3_auto_extension((void *)sqlite3_vec_fs_read_init);
}
