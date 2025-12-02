#ifndef SQLITE_VEC_H
#define SQLITE_VEC_H

#ifndef SQLITE_CORE
#include "sqlite3ext.h"
#else
#include "sqlite3.h"
#endif

#ifdef SQLITE_VEC_STATIC
  #define SQLITE_VEC_API
#else
  #ifdef _WIN32
    #define SQLITE_VEC_API __declspec(dllexport)
  #else
    #define SQLITE_VEC_API
  #endif
#endif

#define SQLITE_VEC_VERSION "v0.2.1-alpha"
// TODO rm
#define SQLITE_VEC_DATE "2025-11-28T18:49:17Z+1100"
#define SQLITE_VEC_SOURCE "50840e1b6c72518964658b2ce0251b8d6b70516c"


#define SQLITE_VEC_VERSION_MAJOR 0
#define SQLITE_VEC_VERSION_MINOR 2
#define SQLITE_VEC_VERSION_PATCH 0

#ifdef __cplusplus
extern "C" {
#endif

SQLITE_VEC_API int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg,
                  const sqlite3_api_routines *pApi);

#ifdef __cplusplus
}  /* end of the 'extern "C"' block */
#endif

#endif /* ifndef SQLITE_VEC_H */
