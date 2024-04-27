#include "sqlite3.h"
#include "sqlite-vec.h"

int sqlite3_wasm_extra_init(const char * unused) {
  return sqlite3_auto_extension((void (*)(void)) sqlite3_vec_init);
}
