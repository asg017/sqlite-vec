#ifndef SQLITE_VEC_H
#define SQLITE_VEC_H

#ifndef SQLITE_CORE
#include "sqlite3ext.h"
#else
#include "sqlite3.h"
#endif

#ifdef SQLITE_VEC_STATIC
  #define SQLITE_VEC_API __attribute__((visibility("default")))
#else
  #ifdef _WIN32
    #define SQLITE_VEC_API __declspec(dllexport)
  #else
    #define SQLITE_VEC_API __attribute__((visibility("default")))
  #endif
#endif

#define SQLITE_VEC_VERSION "v0.2.4-alpha"
// TODO rm
#define SQLITE_VEC_DATE "2026-01-04T19:18:13Z+1100"
#define SQLITE_VEC_SOURCE "c4ec0fc3a6254789d84cfa288313723fb6f2015d"


#define SQLITE_VEC_VERSION_MAJOR 0
#define SQLITE_VEC_VERSION_MINOR 2
#define SQLITE_VEC_VERSION_PATCH 4

#ifdef __cplusplus
extern "C" {
#endif

SQLITE_VEC_API int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg,
                  const sqlite3_api_routines *pApi);

#ifdef __cplusplus
}  /* end of the 'extern "C"' block */
#endif

#endif /* ifndef SQLITE_VEC_H */
