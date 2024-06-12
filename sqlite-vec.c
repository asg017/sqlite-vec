#include "sqlite-vec.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#ifndef UINT32_TYPE
#ifdef HAVE_UINT32_T
#define UINT32_TYPE uint32_t
#else
#define UINT32_TYPE unsigned int
#endif
#endif
#ifndef UINT16_TYPE
#ifdef HAVE_UINT16_T
#define UINT16_TYPE uint16_t
#else
#define UINT16_TYPE unsigned short int
#endif
#endif
#ifndef INT16_TYPE
#ifdef HAVE_INT16_T
#define INT16_TYPE int16_t
#else
#define INT16_TYPE short int
#endif
#endif
#ifndef UINT8_TYPE
#ifdef HAVE_UINT8_T
#define UINT8_TYPE uint8_t
#else
#define UINT8_TYPE unsigned char
#endif
#endif
#ifndef INT8_TYPE
#ifdef HAVE_INT8_T
#define INT8_TYPE int8_t
#else
#define INT8_TYPE signed char
#endif
#endif
#ifndef LONGDOUBLE_TYPE
#define LONGDOUBLE_TYPE long double
#endif

#ifndef _WIN32
#ifndef __EMSCRIPTEN__
#ifndef __COSMOPOLITAN__
typedef u_int8_t uint8_t;
typedef u_int16_t uint16_t;
typedef u_int64_t uint64_t;
#endif
#endif
#endif

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef int32_t i32;
typedef sqlite3_int64 i64;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef size_t usize;

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(X) (void)(X)
#endif

#ifndef todo_assert
#define todo_assert(X) assert(X)
#endif

#define countof(x) (sizeof(x) / sizeof((x)[0]))

#define todo(msg)                                                              \
  do {                                                                         \
    fprintf(stderr, "TODO: %s\n", msg);                                        \
    exit(1);                                                                   \
  } while (0)

enum VectorElementType {
  SQLITE_VEC_ELEMENT_TYPE_FLOAT32 = 223 + 0,
  SQLITE_VEC_ELEMENT_TYPE_BIT = 223 + 1,
  SQLITE_VEC_ELEMENT_TYPE_INT8 = 223 + 2,
};

#ifdef SQLITE_VEC_ENABLE_AVX
#include <immintrin.h>
#define PORTABLE_ALIGN32 __attribute__((aligned(32)))
#define PORTABLE_ALIGN64 __attribute__((aligned(64)))

static f32 l2_sqr_float_avx(const void *pVect1v, const void *pVect2v,
                            const void *qty_ptr) {
  f32 *pVect1 = (f32 *)pVect1v;
  f32 *pVect2 = (f32 *)pVect2v;
  size_t qty = *((size_t *)qty_ptr);
  f32 PORTABLE_ALIGN32 TmpRes[8];
  size_t qty16 = qty >> 4;

  const f32 *pEnd1 = pVect1 + (qty16 << 4);

  __m256 diff, v1, v2;
  __m256 sum = _mm256_set1_ps(0);

  while (pVect1 < pEnd1) {
    v1 = _mm256_loadu_ps(pVect1);
    pVect1 += 8;
    v2 = _mm256_loadu_ps(pVect2);
    pVect2 += 8;
    diff = _mm256_sub_ps(v1, v2);
    sum = _mm256_add_ps(sum, _mm256_mul_ps(diff, diff));

    v1 = _mm256_loadu_ps(pVect1);
    pVect1 += 8;
    v2 = _mm256_loadu_ps(pVect2);
    pVect2 += 8;
    diff = _mm256_sub_ps(v1, v2);
    sum = _mm256_add_ps(sum, _mm256_mul_ps(diff, diff));
  }

  _mm256_store_ps(TmpRes, sum);
  return sqrt(TmpRes[0] + TmpRes[1] + TmpRes[2] + TmpRes[3] + TmpRes[4] +
              TmpRes[5] + TmpRes[6] + TmpRes[7]);
}
#endif

#ifdef SQLITE_VEC_ENABLE_NEON
#include <arm_neon.h>

#define PORTABLE_ALIGN32 __attribute__((aligned(32)))

// thx https://github.com/nmslib/hnswlib/pull/299/files
static f32 l2_sqr_float_neon(const void *pVect1v, const void *pVect2v,
                             const void *qty_ptr) {
  f32 *pVect1 = (f32 *)pVect1v;
  f32 *pVect2 = (f32 *)pVect2v;
  size_t qty = *((size_t *)qty_ptr);
  size_t qty16 = qty >> 4;

  const f32 *pEnd1 = pVect1 + (qty16 << 4);

  float32x4_t diff, v1, v2;
  float32x4_t sum0 = vdupq_n_f32(0);
  float32x4_t sum1 = vdupq_n_f32(0);
  float32x4_t sum2 = vdupq_n_f32(0);
  float32x4_t sum3 = vdupq_n_f32(0);

  while (pVect1 < pEnd1) {
    v1 = vld1q_f32(pVect1);
    pVect1 += 4;
    v2 = vld1q_f32(pVect2);
    pVect2 += 4;
    diff = vsubq_f32(v1, v2);
    sum0 = vfmaq_f32(sum0, diff, diff);

    v1 = vld1q_f32(pVect1);
    pVect1 += 4;
    v2 = vld1q_f32(pVect2);
    pVect2 += 4;
    diff = vsubq_f32(v1, v2);
    sum1 = vfmaq_f32(sum1, diff, diff);

    v1 = vld1q_f32(pVect1);
    pVect1 += 4;
    v2 = vld1q_f32(pVect2);
    pVect2 += 4;
    diff = vsubq_f32(v1, v2);
    sum2 = vfmaq_f32(sum2, diff, diff);

    v1 = vld1q_f32(pVect1);
    pVect1 += 4;
    v2 = vld1q_f32(pVect2);
    pVect2 += 4;
    diff = vsubq_f32(v1, v2);
    sum3 = vfmaq_f32(sum3, diff, diff);
  }

  f32 sum_scalar = vaddvq_f32(vaddq_f32(vaddq_f32(sum0, sum1), vaddq_f32(sum2, sum3)));
  const f32 *pEnd2 = pVect1 + (qty - (qty16 << 4));
  while (pVect1 < pEnd2) {
    f32 diff = *pVect1 - *pVect2;
    sum_scalar += diff * diff;
    pVect1++;
    pVect2++;
  }

  return sqrt(sum_scalar);
}

static f32 l2_sqr_int8_neon(const void *pVect1v, const void *pVect2v,
                            const void *qty_ptr) {
  i8 *pVect1 = (i8 *)pVect1v;
  i8 *pVect2 = (i8 *)pVect2v;
  size_t qty = *((size_t *)qty_ptr);

  const i8 *pEnd1 = pVect1 + qty;
  i32 sum_scalar = 0;

  while (pVect1 < pEnd1 - 7) {
    // loading 8 at a time
    int8x8_t v1 = vld1_s8(pVect1);
    int8x8_t v2 = vld1_s8(pVect2);
    pVect1 += 8;
    pVect2 += 8;

    // widen to protect against overflow
    int16x8_t v1_wide = vmovl_s8(v1);
    int16x8_t v2_wide = vmovl_s8(v2);

    int16x8_t diff = vsubq_s16(v1_wide, v2_wide);
    int16x8_t squared_diff = vmulq_s16(diff, diff);
    int32x4_t sum = vpaddlq_s16(squared_diff);

    sum_scalar += vgetq_lane_s32(sum, 0) + vgetq_lane_s32(sum, 1) +
              vgetq_lane_s32(sum, 2) + vgetq_lane_s32(sum, 3);
  }

  // handle leftovers
  while (pVect1 < pEnd1) {
    i16 diff = (i16)*pVect1 - (i16)*pVect2;
    sum_scalar += diff * diff;
    pVect1++;
    pVect2++;
  }

  return sqrtf(sum_scalar);
}
#endif

static f32 l2_sqr_float(const void *pVect1v, const void *pVect2v,
                        const void *qty_ptr) {
  f32 *pVect1 = (f32 *)pVect1v;
  f32 *pVect2 = (f32 *)pVect2v;
  size_t qty = *((size_t *)qty_ptr);

  f32 res = 0;
  for (size_t i = 0; i < qty; i++) {
    f32 t = *pVect1 - *pVect2;
    pVect1++;
    pVect2++;
    res += t * t;
  }
  return sqrt(res);
}

static f32 l2_sqr_int8(const void *pA, const void *pB, const void *pD) {
  i8 *a = (i8 *)pA;
  i8 *b = (i8 *)pB;
  size_t d = *((size_t *)pD);

  f32 res = 0;
  for (size_t i = 0; i < d; i++) {
    f32 t = *a - *b;
    a++;
    b++;
    res += t * t;
  }
  return sqrt(res);
}

static f32 distance_l2_sqr_float(const void *a, const void *b, const void *d) {
#ifdef SQLITE_VEC_ENABLE_NEON
  if ((*(const size_t *)d) > 16) {
    return l2_sqr_float_neon(a, b, d);
  }
#endif
#ifdef SQLITE_VEC_ENABLE_AVX
  if (((*(const size_t *)d) % 16 == 0)) {
    return l2_sqr_float_avx(a, b, d);
  }
#endif
  return l2_sqr_float(a, b, d);
}

static f32 distance_l2_sqr_int8(const void *a, const void *b, const void *d) {
  #ifdef SQLITE_VEC_ENABLE_NEON
  if ((*(const size_t *)d) > 7) {
    return l2_sqr_int8_neon(a, b, d);
  }
  #endif
  return l2_sqr_int8(a, b, d);
}

static f32 distance_cosine_float(const void *pVect1v, const void *pVect2v,
                                 const void *qty_ptr) {
  f32 *pVect1 = (f32 *)pVect1v;
  f32 *pVect2 = (f32 *)pVect2v;
  size_t qty = *((size_t *)qty_ptr);

  f32 dot = 0;
  f32 aMag = 0;
  f32 bMag = 0;
  for (size_t i = 0; i < qty; i++) {
    dot += *pVect1 * *pVect2;
    aMag += *pVect1 * *pVect1;
    bMag += *pVect2 * *pVect2;
    pVect1++;
    pVect2++;
  }
  return 1 - (dot / (sqrt(aMag) * sqrt(bMag)));
}
static f32 distance_cosine_int8(const void *pA, const void *pB,
                                const void *pD) {
  i8 *a = (i8 *)pA;
  i8 *b = (i8 *)pB;
  size_t d = *((size_t *)pD);

  f32 dot = 0;
  f32 aMag = 0;
  f32 bMag = 0;
  for (size_t i = 0; i < d; i++) {
    dot += *a * *b;
    aMag += *a * *a;
    bMag += *b * *b;
    a++;
    b++;
  }
  return 1 - (dot / (sqrt(aMag) * sqrt(bMag)));
}

// https://github.com/facebookresearch/faiss/blob/77e2e79cd0a680adc343b9840dd865da724c579e/faiss/utils/hamming_distance/common.h#L34
static u8 hamdist_table[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4,
    2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4,
    2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6,
    4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5,
    3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6,
    4, 5, 5, 6, 5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};

static f32 distance_hamming_u8(u8 *a, u8 *b, size_t n) {
  int same = 0;
  for (unsigned long i = 0; i < n; i++) {
    same += hamdist_table[a[i] ^ b[i]];
  }
  return (f32)same;
}

#ifdef _MSC_VER
#  include <intrin.h>
#  define __builtin_popcountl __popcnt64
#endif

static f32 distance_hamming_u64(u64 *a, u64 *b, size_t n) {
  int same = 0;
  for (unsigned long i = 0; i < n; i++) {
    same += __builtin_popcountl(a[i] ^ b[i]);
  }
  return (f32)same;
}

/**
 * @brief Calculate the hamming distance between two bitvectors.
 *
 * @param a - first bitvector, MUST have d dimensions
 * @param b - second bitvector, MUST have d dimensions
 * @param d - pointer to size_t, MUST be divisible by CHAR_BIT
 * @return f32
 */
static f32 distance_hamming(const void *a, const void *b, const void *d) {
  size_t dimensions = *((size_t *)d);

  if ((dimensions % 64) == 0) {
    return distance_hamming_u64((u64 *)a, (u64 *)b, dimensions / 8 / CHAR_BIT);
  }
  return distance_hamming_u8((u8 *)a, (u8 *)b, dimensions / CHAR_BIT);
}

// from SQLite source:
// https://github.com/sqlite/sqlite/blob/a509a90958ddb234d1785ed7801880ccb18b497e/src/json.c#L153
static const char jsonIsSpace[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
#define jsonIsspace(x) (jsonIsSpace[(unsigned char)x])

typedef void (*vector_cleanup)(void *p);

void vector_cleanup_noop(void *_) { UNUSED_PARAMETER(_); }

#define JSON_SUBTYPE 74

struct Array {
  size_t element_size;
  size_t length;
  size_t capacity;
  void *z;
};

/**
 * @brief Initial an array with the given element size and capacity.
 *
 * @param array
 * @param element_size
 * @param init_capacity
 * @return SQLITE_OK on success, error code on failure. Only error is SQLITE_NOMEM
 */
int array_init(struct Array *array, size_t element_size, size_t init_capacity) {
  void *z = sqlite3_malloc(element_size * init_capacity);
  if (!z) {
    return SQLITE_NOMEM;
  }
  array->element_size = element_size;
  array->length = 0;
  array->capacity = init_capacity;
  array->z = z;
  return SQLITE_OK;
}

int array_append(struct Array *array, const void *element) {
  if (array->length == array->capacity) {
    size_t new_capacity = array->capacity * 2 + 100;
    void *z = sqlite3_realloc64(array->z, array->element_size * new_capacity);
    if (z) {
      array->capacity = new_capacity;
      array->z = z;
    } else {
      return SQLITE_NOMEM;
    }
  }
  memcpy(&array->z[array->length * array->element_size], element,
         array->element_size);
  array->length++;
  return SQLITE_OK;
}

void array_cleanup(struct Array *array) {
  array->element_size = 0;
  array->length = 0;
  array->capacity = 0;
  sqlite3_free(array->z);
  array->z = NULL;
}

typedef void (*fvec_cleanup)(f32 *vector);

void fvec_cleanup_noop(f32 *_) { UNUSED_PARAMETER(_); }

static int fvec_from_value(sqlite3_value *value, f32 **vector,
                           size_t *dimensions, fvec_cleanup *cleanup,
                           char **pzErr) {
  int value_type = sqlite3_value_type(value);

  if (value_type == SQLITE_BLOB) {
    const void *blob = sqlite3_value_blob(value);
    int bytes = sqlite3_value_bytes(value);
    if (bytes == 0) {
      *pzErr = sqlite3_mprintf("zero-length vectors are not supported.");
      return SQLITE_ERROR;
    }
    if ((bytes % sizeof(f32)) != 0) {
      *pzErr = sqlite3_mprintf("invalid float32 vector BLOB length. Must be "
                               "divisible by %d, found %d",
                               sizeof(f32), bytes);
      return SQLITE_ERROR;
    }
    *vector = (f32 *)blob;
    *dimensions = bytes / sizeof(f32);
    *cleanup = fvec_cleanup_noop;
    return SQLITE_OK;
  }

  if (value_type == SQLITE_TEXT) {
    const char *source = (const char *)sqlite3_value_text(value);
    int source_len = sqlite3_value_bytes(value);
    int i = 0;

    struct Array x;
    int rc = array_init(&x, sizeof(f32), ceil(source_len / 2.0));
    if(rc != SQLITE_OK) {
      return rc;
    }

    // advance leading whitespace to first '['
    while (i < source_len) {
      if (jsonIsspace(source[i])) {
        i++;
        continue;
      }
      if (source[i] == '[') {
        break;
      }

      *pzErr = sqlite3_mprintf(
          "JSON array parsing error: Input does not start with '['");
      array_cleanup(&x);
      return SQLITE_ERROR;
    }
    if (source[i] != '[') {
      *pzErr = sqlite3_mprintf(
          "JSON array parsing error: Input does not start with '['");
      array_cleanup(&x);
      return SQLITE_ERROR;
    }
    int offset = i + 1;

    while (offset < source_len) {
      char *ptr = (char *)&source[offset];
      char *endptr;

      errno = 0;
      double result = strtod(ptr, &endptr);
      if ((errno != 0 && result == 0) // some interval error?
          || (errno == ERANGE &&
              (result == HUGE_VAL || result == -HUGE_VAL)) // too big / smalls
      ) {
        sqlite3_free(x.z);
        *pzErr = sqlite3_mprintf("JSON parsing error");
        return SQLITE_ERROR;
      }

      if (endptr == ptr) {
        if (*ptr != ']') {
          sqlite3_free(x.z);
          *pzErr = sqlite3_mprintf("JSON parsing error");
          return SQLITE_ERROR;
        }
        goto done;
      }

      f32 res = (f32)result;
      array_append(&x, (const void *)&res);

      offset += (endptr - ptr);
      while (offset < source_len) {
        if (jsonIsspace(source[offset])) {
          offset++;
          continue;
        }
        if (source[offset] == ',') {
          offset++;
          continue;
        } // TODO multiple commas in a row without digits?
        if (source[offset] == ']')
          goto done;
        break;
      }
    }

  done:

    if (x.length > 0) {
      *vector = (f32 *)x.z;
      *dimensions = x.length;
      *cleanup = (fvec_cleanup)sqlite3_free;
      return SQLITE_OK;
    }
    sqlite3_free(x.z);
    *pzErr = sqlite3_mprintf("zero-length vectors are not supported.");
    return SQLITE_ERROR;
  }

  *pzErr = sqlite3_mprintf(
      "Input must have type BLOB (compact format) or TEXT (JSON)");
  return SQLITE_ERROR;
}

static int bitvec_from_value(sqlite3_value *value, u8 **vector,
                             size_t *dimensions, vector_cleanup *cleanup,
                             char **pzErr) {
  int value_type = sqlite3_value_type(value);
  if (value_type == SQLITE_BLOB) {
    const void *blob = sqlite3_value_blob(value);
    int bytes = sqlite3_value_bytes(value);
    if (bytes == 0) {
      *pzErr = sqlite3_mprintf("zero-length vectors are not supported.");
      return SQLITE_ERROR;
    }
    *vector = (u8 *)blob;
    *dimensions = bytes * CHAR_BIT;
    *cleanup = vector_cleanup_noop;
    return SQLITE_OK;
  }
  *pzErr = sqlite3_mprintf("Unknown type for bitvector.");
  return SQLITE_ERROR;
}

static int int8_vec_from_value(sqlite3_value *value, i8 **vector,
                               size_t *dimensions, vector_cleanup *cleanup,
                               char **pzErr) {
  int value_type = sqlite3_value_type(value);
  if (value_type == SQLITE_BLOB) {
    const void *blob = sqlite3_value_blob(value);
    int bytes = sqlite3_value_bytes(value);
    if (bytes == 0) {
      *pzErr = sqlite3_mprintf("zero-length vectors are not supported.");
      return SQLITE_ERROR;
    }
    *vector = (i8 *)blob;
    *dimensions = bytes;
    *cleanup = vector_cleanup_noop;
    return SQLITE_OK;
  }

 if (value_type == SQLITE_TEXT) {
    const char *source = (const char *)sqlite3_value_text(value);
    int source_len = sqlite3_value_bytes(value);
    int i = 0;

    struct Array x;
    int rc = array_init(&x, sizeof(i8), ceil(source_len / 2.0));
    if (rc != SQLITE_OK) {
      return rc;
    }

    // advance leading whitespace to first '['
    while (i < source_len) {
      if (jsonIsspace(source[i])) {
        i++;
        continue;
      }
      if (source[i] == '[') {
        break;
      }

      *pzErr = sqlite3_mprintf(
          "JSON array parsing error: Input does not start with '['");
      array_cleanup(&x);
      return SQLITE_ERROR;
    }
    if (source[i] != '[') {
      *pzErr = sqlite3_mprintf(
          "JSON array parsing error: Input does not start with '['");
      array_cleanup(&x);
      return SQLITE_ERROR;
    }
    int offset = i + 1;

    while (offset < source_len) {
      char *ptr = (char *)&source[offset];
      char *endptr;

      errno = 0;
      long result = strtol(ptr, &endptr, 10);
      if ((errno != 0 && result == 0)
          || (errno == ERANGE &&
              (result == LONG_MAX || result == LONG_MIN))
      ) {
        sqlite3_free(x.z);
        *pzErr = sqlite3_mprintf("JSON parsing error");
        return SQLITE_ERROR;
      }

      if (endptr == ptr) {
        if (*ptr != ']') {
          sqlite3_free(x.z);
          *pzErr = sqlite3_mprintf("JSON parsing error");
          return SQLITE_ERROR;
        }
        goto done;
      }

      if (result < INT8_MIN || result > INT8_MAX) {
        sqlite3_free(x.z);
        *pzErr = sqlite3_mprintf("JSON parsing error: value out of range for int8");
        return SQLITE_ERROR;
      }

      i8 res = (i8)result;
      array_append(&x, (const void *)&res);

      offset += (endptr - ptr);
      while (offset < source_len) {
        if (jsonIsspace(source[offset])) {
          offset++;
          continue;
        }
        if (source[offset] == ',') {
          offset++;
          continue;
        }
        if (source[offset] == ']')
          goto done;
        break;
      }
    }

  done:

    if (x.length > 0) {
      *vector = (i8 *)x.z;
      *dimensions = x.length;
      *cleanup = (vector_cleanup)sqlite3_free;
      return SQLITE_OK;
    }
    sqlite3_free(x.z);
    *pzErr = sqlite3_mprintf("zero-length vectors are not supported.");
    return SQLITE_ERROR;
  }

  *pzErr = sqlite3_mprintf("Unknown type for int8 vector.");
  return SQLITE_ERROR;
}

/**
 * @brief Extract a vector from a sqlite3_value. Can be a float32, int8, or bit
 * vector.
 *
 * @param value: the sqlite3_value to read from.
 * @param vector: Output pointer to vector data.
 * @param dimensions: Output number of dimensions
 * @param dimensions: Output vector element type
 * @param cleanup
 * @param pzErrorMessage
 * @return int SQLITE_OK on success, error code otherwise
 */
int vector_from_value(sqlite3_value *value, void **vector, size_t *dimensions,
                      enum VectorElementType *element_type,
                      vector_cleanup *cleanup, char **pzErrorMessage) {
  int subtype = sqlite3_value_subtype(value);
  if (!subtype || (subtype == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) ||
      (subtype == JSON_SUBTYPE)) {
    int rc = fvec_from_value(value, (f32 **)vector, dimensions,
                             (fvec_cleanup *)cleanup, pzErrorMessage);
    if (rc == SQLITE_OK) {
      *element_type = SQLITE_VEC_ELEMENT_TYPE_FLOAT32;
    }
    return rc;
  }

  if (subtype == SQLITE_VEC_ELEMENT_TYPE_BIT) {
    int rc = bitvec_from_value(value, (u8 **)vector, dimensions, cleanup,
                               pzErrorMessage);
    if (rc == SQLITE_OK) {
      *element_type = SQLITE_VEC_ELEMENT_TYPE_BIT;
    }
    return rc;
  }
  if (subtype == SQLITE_VEC_ELEMENT_TYPE_INT8) {
    int rc = int8_vec_from_value(value, (i8 **)vector, dimensions, cleanup,
                                 pzErrorMessage);
    if (rc == SQLITE_OK) {
      *element_type = SQLITE_VEC_ELEMENT_TYPE_INT8;
    }
    return rc;
  }
  *pzErrorMessage = sqlite3_mprintf("Unknown subtype: %d", subtype);
  return SQLITE_ERROR;
}

char *vector_subtype_name(int subtype) {
  switch (subtype) {
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32:
    return "float32";
  case SQLITE_VEC_ELEMENT_TYPE_INT8:
    return "int8";
  case SQLITE_VEC_ELEMENT_TYPE_BIT:
    return "bit";
  }
  return "";
}
int ensure_vector_match(sqlite3_value *aValue, sqlite3_value *bValue, void **a,
                        void **b, enum VectorElementType *element_type,
                        size_t *dimensions, vector_cleanup *outACleanup,
                        vector_cleanup *outBCleanup, char **outError) {
  int rc;
  enum VectorElementType aType, bType;
  size_t aDims, bDims;
  char *error;
  vector_cleanup aCleanup, bCleanup;

  rc = vector_from_value(aValue, a, &aDims, &aType, &aCleanup, &error);
  if (rc != SQLITE_OK) {
    *outError = sqlite3_mprintf("Error reading 1st vector: %s", error);
    sqlite3_free(error);
    return SQLITE_ERROR;
  }

  rc = vector_from_value(bValue, b, &bDims, &bType, &bCleanup, &error);
  if (rc != SQLITE_OK) {
    *outError = sqlite3_mprintf("Error reading 2nd vector: %s", error);
    sqlite3_free(error);
    aCleanup(a);
    return SQLITE_ERROR;
  }

  if (aType != bType) {
    *outError =
        sqlite3_mprintf("Vector type mistmatch. First vector has type %s, "
                        "while the second has type %s.",
                        vector_subtype_name(aType), vector_subtype_name(bType));
    aCleanup(a);
    bCleanup(b);
    return SQLITE_ERROR;
  }
  if (aDims != bDims) {
    *outError = sqlite3_mprintf(
        "Vector dimension mistmatch. First vector has %ld dimensions, "
        "while the second has %ld dimensions.",
        aDims, bDims);
    aCleanup(a);
    bCleanup(b);
    return SQLITE_ERROR;
  }
  *element_type = aType;
  *dimensions = aDims;
  *outACleanup = aCleanup;
  *outBCleanup = bCleanup;
  return SQLITE_OK;
}

int _cmp(const void *a, const void *b) { return (*(i64 *)a - *(i64 *)b); }

struct VecNpyFile {
  char *path;
  size_t pathLength;
};
#define SQLITE_VEC_NPY_FILE_NAME "vec0-npy-file"

static void vec_npy_file(sqlite3_context *context, int argc,
                         sqlite3_value **argv) {
  todo_assert(argc == 1);
  char *path = (char *)sqlite3_value_text(argv[0]);
  size_t pathLength = sqlite3_value_bytes(argv[0]);
  struct VecNpyFile *f = sqlite3_malloc(sizeof(struct VecNpyFile));
  f->path = path;
  f->pathLength = pathLength;
  sqlite3_result_pointer(context, f, SQLITE_VEC_NPY_FILE_NAME, sqlite3_free);
}

static void vec_f32(sqlite3_context *context, int argc, sqlite3_value **argv) {
  todo_assert(argc == 1);
  int rc;
  f32 *vector;
  size_t dimensions;
  fvec_cleanup cleanup;
  char *errmsg;
  rc = fvec_from_value(argv[0], &vector, &dimensions, &cleanup, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  sqlite3_result_blob(context, vector, dimensions * sizeof(f32),
                      SQLITE_TRANSIENT);
  sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
  cleanup(vector);
}
static void vec_bit(sqlite3_context *context, int argc, sqlite3_value **argv) {
  todo_assert(argc == 1);
  int rc;
  u8 *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *errmsg;
  rc = bitvec_from_value(argv[0], &vector, &dimensions, &cleanup, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  sqlite3_result_blob(context, vector, dimensions / CHAR_BIT, SQLITE_TRANSIENT);
  sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_BIT);
  cleanup(vector);
}
static void vec_int8(sqlite3_context *context, int argc, sqlite3_value **argv) {
  todo_assert(argc == 1);
  int rc;
  i8 *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *errmsg;
  rc = int8_vec_from_value(argv[0], &vector, &dimensions, &cleanup, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  sqlite3_result_blob(context, vector, dimensions, SQLITE_TRANSIENT);
  sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_INT8);
  cleanup(vector);
}

static void vec_length(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  todo_assert(argc == 1);
  int rc;
  void *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *errmsg;
  enum VectorElementType elementType;
  rc = vector_from_value(argv[0], &vector, &dimensions, &elementType, &cleanup,
                         &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, errmsg, -1);
    sqlite3_free(errmsg);
    return;
  }
  sqlite3_result_int64(context, dimensions);
  cleanup(vector);
}

static void vec_distance_cosine(sqlite3_context *context, int argc,
                                sqlite3_value **argv) {
  todo_assert(argc == 2);
  int rc;
  void *a, *b;
  size_t dimensions;
  vector_cleanup aCleanup, bCleanup;
  char *error;
  enum VectorElementType elementType;
  rc = ensure_vector_match(argv[0], argv[1], &a, &b, &elementType, &dimensions,
                           &aCleanup, &bCleanup, &error);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, error, -1);
    sqlite3_free(error);
    return;
  }

  switch (elementType) {
  case SQLITE_VEC_ELEMENT_TYPE_BIT: {
    sqlite3_result_error(
        context, "Cannot calculate cosine distance between two bitvectors.",
        -1);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
    f32 result = distance_cosine_float(a, b, &dimensions);
    sqlite3_result_double(context, result);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_INT8: {
    f32 result = distance_cosine_int8(a, b, &dimensions);
    sqlite3_result_double(context, result);
    goto finish;
  }
  }

finish:
  aCleanup(a);
  bCleanup(b);
  return;
}

static void vec_distance_l2(sqlite3_context *context, int argc,
                            sqlite3_value **argv) {
  todo_assert(argc == 2);
  int rc;
  void *a, *b;
  size_t dimensions;
  vector_cleanup aCleanup, bCleanup;
  char *error;
  enum VectorElementType elementType;
  rc = ensure_vector_match(argv[0], argv[1], &a, &b, &elementType, &dimensions,
                           &aCleanup, &bCleanup, &error);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, error, -1);
    sqlite3_free(error);
    return;
  }

  switch (elementType) {
  case SQLITE_VEC_ELEMENT_TYPE_BIT: {
    sqlite3_result_error(
        context, "Cannot calculate L2 distance between two bitvectors.", -1);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
    f32 result = distance_l2_sqr_float(a, b, &dimensions);
    sqlite3_result_double(context, result);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_INT8: {
    f32 result = distance_l2_sqr_int8(a, b, &dimensions);
    sqlite3_result_double(context, result);
    goto finish;
  }
  }

finish:
  aCleanup(a);
  bCleanup(b);
  return;
}
static void vec_distance_hamming(sqlite3_context *context, int argc,
                                 sqlite3_value **argv) {
  todo_assert(argc == 2);
  int rc;
  void *a, *b;
  size_t dimensions;
  vector_cleanup aCleanup, bCleanup;
  char *error;
  enum VectorElementType elementType;
  rc = ensure_vector_match(argv[0], argv[1], &a, &b, &elementType, &dimensions,
                           &aCleanup, &bCleanup, &error);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, error, -1);
    sqlite3_free(error);
    return;
  }

  switch (elementType) {
  case SQLITE_VEC_ELEMENT_TYPE_BIT: {
    sqlite3_result_double(context, distance_hamming(a, b, &dimensions));
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
    sqlite3_result_error(
        context,
        "Cannot calculate hamming distance between two float32 vectors.", -1);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_INT8: {
    sqlite3_result_error(
        context, "Cannot calculate hamming distance between two int8 vectors.",
        -1);
    goto finish;
  }
  }

finish:
  aCleanup(a);
  bCleanup(b);
  return;
}

static void vec_quantize_i8(sqlite3_context *context, int argc,
                            sqlite3_value **argv) {
  f32 *srcVector;
  size_t dimensions;
  fvec_cleanup cleanup;
  char *err;
  int rc = fvec_from_value(argv[0], &srcVector, &dimensions, &cleanup, &err);
  assert(rc == SQLITE_OK);
  i8 *out = sqlite3_malloc(dimensions * sizeof(i8));
  assert(out);

  if (argc == 2) {
    if ((sqlite3_value_type(argv[1]) != SQLITE_TEXT) ||
        (sqlite3_value_bytes(argv[1]) != strlen("unit")) ||
        (sqlite3_stricmp((const char *)sqlite3_value_text(argv[1]), "unit") !=
         0)) {
      sqlite3_result_error(context,
                           "2nd argument to vec_quantize_i8() must be 'unit', "
                           "or ranges must be provided.",
                           -1);
      cleanup(srcVector);
      sqlite3_free(out);
      return;
    }
    f32 step = (1.0 - (-1.0)) / 255;
    for (size_t i = 0; i < dimensions; i++) {
      out[i] = ((srcVector[i] - (-1.0)) / step) - 128;
    }
  } else if (argc == 3) {
    // f32 * minVector, maxVector;
    // size_t d;
    // fvec_cleanup minCleanup, maxCleanup;
    // int rc = fvec_from_value(argv[1], )
    todo("ranges");
  }

  cleanup(srcVector);
  sqlite3_result_blob(context, out, dimensions * sizeof(i8), sqlite3_free);
  sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_INT8);
  return;
}

static void vec_quantize_binary(sqlite3_context *context, int argc,
                                sqlite3_value **argv) {
  todo_assert(argc == 1);
  void *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *pzError;
  enum VectorElementType elementType;
  int rc = vector_from_value(argv[0], &vector, &dimensions, &elementType,
                             &cleanup, &pzError);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, pzError, -1);
    sqlite3_free(pzError);
    return;
  }

  if (elementType == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
    u8 *out = sqlite3_malloc(dimensions / CHAR_BIT);
    if(!out) {
      cleanup(vector);
      sqlite3_result_error_code(context, SQLITE_NOMEM);
      return;
    }
    for (size_t i = 0; i < dimensions; i++) {
      int res = ((f32 *)vector)[i] > 0.0;
      out[i / 8] |= (res << (i % 8));
    }
    sqlite3_result_blob(context, out, dimensions / CHAR_BIT, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_BIT);
  } else if (elementType == SQLITE_VEC_ELEMENT_TYPE_INT8) {
    u8 *out = sqlite3_malloc(dimensions / CHAR_BIT);
    if(!out) {
      cleanup(vector);
      sqlite3_result_error_code(context, SQLITE_NOMEM);
      return;
    }
    for (size_t i = 0; i < dimensions; i++) {
      int res = ((i8 *)vector)[i] > 0;
      out[i / 8] |= (res << (i % 8));
    }
    sqlite3_result_blob(context, out, dimensions / CHAR_BIT, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_BIT);
  } else {
    sqlite3_result_error(context, "Can only binary quantize float or int8 vectors", -1);
    return;
  }
}

static void vec_add(sqlite3_context *context, int argc, sqlite3_value **argv) {
  todo_assert(argc == 2);
  int rc;
  void *a, *b;
  size_t dimensions;
  vector_cleanup aCleanup, bCleanup;
  char *error;
  enum VectorElementType elementType;
  rc = ensure_vector_match(argv[0], argv[1], &a, &b, &elementType, &dimensions,
                           &aCleanup, &bCleanup, &error);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, error, -1);
    sqlite3_free(error);
    return;
  }

  switch (elementType) {
  case SQLITE_VEC_ELEMENT_TYPE_BIT: {
    sqlite3_result_error(context, "Cannot add two bitvectors together.", -1);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
    size_t outSize = dimensions * sizeof(f32);
    f32 *out = sqlite3_malloc(outSize);
    if (!out) {
      sqlite3_result_error_nomem(context);
      goto finish;
    }
    for (size_t i = 0; i < dimensions; i++) {
      out[i] = ((f32 *)a)[i] + ((f32 *)b)[i];
    }
    sqlite3_result_blob(context, out, outSize, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_INT8: {
    size_t outSize = dimensions * sizeof(i8);
    i8 *out = sqlite3_malloc(outSize);
    if (!out) {
      sqlite3_result_error_nomem(context);
      goto finish;
    }
    for (size_t i = 0; i < dimensions; i++) {
      out[i] = ((i8 *)a)[i] + ((i8 *)b)[i];
    }
    sqlite3_result_blob(context, out, outSize, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_INT8);
    goto finish;
  }
  }
finish:
  aCleanup(a);
  bCleanup(b);
  return;
}
static void vec_sub(sqlite3_context *context, int argc, sqlite3_value **argv) {
  todo_assert(argc == 2);
  int rc;
  void *a, *b;
  size_t dimensions;
  vector_cleanup aCleanup, bCleanup;
  char *error;
  enum VectorElementType elementType;
  rc = ensure_vector_match(argv[0], argv[1], &a, &b, &elementType, &dimensions,
                           &aCleanup, &bCleanup, &error);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, error, -1);
    sqlite3_free(error);
    return;
  }

  switch (elementType) {
  case SQLITE_VEC_ELEMENT_TYPE_BIT: {
    sqlite3_result_error(context, "Cannot subtract two bitvectors together.",
                         -1);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
    size_t outSize = dimensions * sizeof(f32);
    f32 *out = sqlite3_malloc(outSize);
    if (!out) {
      sqlite3_result_error_nomem(context);
      goto finish;
    }
    for (size_t i = 0; i < dimensions; i++) {
      out[i] = ((f32 *)a)[i] - ((f32 *)b)[i];
    }
    sqlite3_result_blob(context, out, outSize, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
    goto finish;
  }
  case SQLITE_VEC_ELEMENT_TYPE_INT8: {
    size_t outSize = dimensions * sizeof(i8);
    i8 *out = sqlite3_malloc(outSize);
    if (!out) {
      sqlite3_result_error_nomem(context);
      goto finish;
    }
    for (size_t i = 0; i < dimensions; i++) {
      out[i] = ((i8 *)a)[i] - ((i8 *)b)[i];
    }
    sqlite3_result_blob(context, out, outSize, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_INT8);
    goto finish;
  }
  }
finish:
  aCleanup(a);
  bCleanup(b);
  return;
}
static void vec_slice(sqlite3_context *context, int argc,
                      sqlite3_value **argv) {
  todo_assert(argc == 3);

  void *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *err;
  enum VectorElementType elementType;

  int rc = vector_from_value(argv[0], &vector, &dimensions, &elementType,
                             &cleanup, &err);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, err, -1);
    sqlite3_free(err);
    return;
  }

  int start = sqlite3_value_int(argv[1]);
  int end = sqlite3_value_int(argv[2]);
  if (start < 0) {
    sqlite3_result_error(context,
                         "slice 'start' index must be a postive number.", -1);
    goto done;
  }
  if (end < 0) {
    sqlite3_result_error(context, "slice 'end' index must be a postive number.",
                         -1);
    goto done;
  }
  if (((size_t)start) > dimensions) {
    sqlite3_result_error(
        context, "slice 'start' index is greater than the number of dimensions",
        -1);
    goto done;
  }
  if (((size_t)end) > dimensions) {
    sqlite3_result_error(
        context, "slice 'end' index is greater than the number of dimensions",
        -1);
    goto done;
  }
  if (start > end) {
    sqlite3_result_error(context,
                         "slice 'start' index is greater than 'end' index", -1);
    goto done;
  }
  // TODO check start == end
  size_t n = end - start;

  switch (elementType) {
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
    f32 *out = sqlite3_malloc(n * sizeof(f32));
    if (!out) {
      sqlite3_result_error_nomem(context);
      return;
    }
    for (size_t i = 0; i < n; i++) {
      out[i] = ((f32 *)vector)[start + i];
    }
    sqlite3_result_blob(context, out, n * sizeof(f32), sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
    goto done;
  }
  case SQLITE_VEC_ELEMENT_TYPE_INT8: {
    i8 *out = sqlite3_malloc(n * sizeof(i8));
    if (!out) {
      sqlite3_result_error_nomem(context);
      return;
    }
    for (size_t i = 0; i < n; i++) {
      out[i] = ((i8 *)vector)[start + i];
    }
    sqlite3_result_blob(context, out, n * sizeof(i8), sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_INT8);
    goto done;
  }
  case SQLITE_VEC_ELEMENT_TYPE_BIT: {
    if ((start % CHAR_BIT) != 0) {
      sqlite3_result_error(context, "start index must be divisible by 8.", -1);
      goto done;
    }
    if ((end % CHAR_BIT) != 0) {
      sqlite3_result_error(context, "end index must be divisible by 8.", -1);
      goto done;
    }

    u8 *out = sqlite3_malloc(n / CHAR_BIT);
    if (!out) {
      sqlite3_result_error_nomem(context);
      return;
    }
    for (size_t i = 0; i < n / CHAR_BIT; i++) {
      out[i] = ((u8 *)vector)[(start / CHAR_BIT) + i];
    }
    sqlite3_result_blob(context, out, n / CHAR_BIT, sqlite3_free);
    sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_BIT);
    goto done;
  }
  }
done:
  cleanup(vector);
}

static void vec_to_json(sqlite3_context *context, int argc,
                        sqlite3_value **argv) {
  todo_assert(argc == 1);
  void *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *err;
  enum VectorElementType elementType;

  int rc = vector_from_value(argv[0], &vector, &dimensions, &elementType,
                             &cleanup, &err);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, err, -1);
    sqlite3_free(err);
    return;
  }

  sqlite3_str *str = sqlite3_str_new(sqlite3_context_db_handle(context));
  sqlite3_str_appendall(str, "[");
  for (size_t i = 0; i < dimensions; i++) {
    if (i != 0) {
      sqlite3_str_appendall(str, ",");
    }
    if (elementType == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
      f32 value = ((f32 *)vector)[i];
      if(isnan(value)) {
        sqlite3_str_appendall(str, "null");
      }
      else {
        sqlite3_str_appendf(str, "%f", value);
      }

    } else if (elementType == SQLITE_VEC_ELEMENT_TYPE_INT8) {
      sqlite3_str_appendf(str, "%d", ((i8 *)vector)[i]);
    } else if (elementType == SQLITE_VEC_ELEMENT_TYPE_BIT) {
      u8 b = (((u8 *)vector)[i / 8] >> (i % CHAR_BIT)) & 1;
      sqlite3_str_appendf(str, "%d", b);
    }
  }
  sqlite3_str_appendall(str, "]");
  int len = sqlite3_str_length(str);
  char *s = sqlite3_str_finish(str);
  if (s) {
    sqlite3_result_text(context, s, len, sqlite3_free);
    sqlite3_result_subtype(context, JSON_SUBTYPE);
  } else {
    sqlite3_result_error_nomem(context);
  }
  cleanup(vector);
}

static void vec_normalize(sqlite3_context *context, int argc,
                          sqlite3_value **argv) {
  todo_assert(argc == 1);
  void *vector;
  size_t dimensions;
  vector_cleanup cleanup;
  char *err;
  enum VectorElementType elementType;

  int rc = vector_from_value(argv[0], &vector, &dimensions, &elementType,
                             &cleanup, &err);
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, err, -1);
    sqlite3_free(err);
    return;
  }

  if (elementType != SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
    sqlite3_result_error(
        context, "only float32 vectors are supported when normalizing", -1);
    cleanup(vector);
    return;
  }

  f32 *out = sqlite3_malloc(dimensions * sizeof(f32));
  if(!out) {
    cleanup(vector);
    sqlite3_result_error_code(context, SQLITE_NOMEM);
    return;
  }

  f32 *v = (f32 *)vector;

  f32 norm = 0;
  for (size_t i = 0; i < dimensions; i++) {
    norm += v[i] * v[i];
  }
  norm = sqrt(norm);
  for (size_t i = 0; i < dimensions; i++) {
    out[i] = v[i] / norm;
  }

  sqlite3_result_blob(context, out, dimensions * sizeof(f32), sqlite3_free);
  sqlite3_result_subtype(context, SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
  cleanup(vector);
}

static void _static_text_func(sqlite3_context *context, int argc,
                              sqlite3_value **argv) {
  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);
  sqlite3_result_text(context, sqlite3_user_data(context), -1, SQLITE_STATIC);
}

enum Vec0TokenType {
  TOKEN_TYPE_IDENTIFIER,
  TOKEN_TYPE_DIGIT,
  TOKEN_TYPE_LBRACKET,
  TOKEN_TYPE_RBRACKET,
  TOKEN_TYPE_EQ,
};
struct Vec0Token {
  enum Vec0TokenType token_type;
  char *start;
  char *end;
};

int is_alpha(char x) {
  return (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z');
}
int is_digit(char x) { return (x >= '0' && x <= '9'); }
int is_whitespace(char x) {
  return x == ' ' || x == '\t' || x == '\n' || x == '\r';
}

#define VEC0_TOKEN_RESULT_EOF 1
#define VEC0_TOKEN_RESULT_SOME 2
#define VEC0_TOKEN_RESULT_ERROR 3

int vec0_token_next(char *start, char *end, struct Vec0Token *out) {
  char *ptr = start;
  while (ptr < end) {
    char curr = *ptr;
    if (is_whitespace(curr)) {
      ptr++;
      continue;
    } else if (curr == '[') {
      ptr++;
      out->start = ptr;
      out->end = ptr;
      out->token_type = TOKEN_TYPE_LBRACKET;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == ']') {
      ptr++;
      out->start = ptr;
      out->end = ptr;
      out->token_type = TOKEN_TYPE_RBRACKET;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == '=') {
      ptr++;
      out->start = ptr;
      out->end = ptr;
      out->token_type = TOKEN_TYPE_EQ;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (is_alpha(curr)) {
      char *start = ptr;
      while (ptr < end && (is_alpha(*ptr) || is_digit(*ptr) || *ptr == '_')) {
        ptr++;
      }
      out->start = start;
      out->end = ptr;
      out->token_type = TOKEN_TYPE_IDENTIFIER;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (is_digit(curr)) {
      char *start = ptr;
      while (ptr < end && (is_digit(*ptr))) {
        ptr++;
      }
      out->start = start;
      out->end = ptr;
      out->token_type = TOKEN_TYPE_DIGIT;
      return VEC0_TOKEN_RESULT_SOME;
    } else {
      return VEC0_TOKEN_RESULT_ERROR;
    }
  }
  return VEC0_TOKEN_RESULT_EOF;
}

struct Vec0Scanner {
  char *start;
  char *end;
  char *ptr;
};

void vec0_scanner_init(struct Vec0Scanner *scanner, const char *source,
                       int source_length) {
  scanner->start = (char *)source;
  scanner->end = (char *)source + source_length;
  scanner->ptr = (char *)source;
}
int vec0_scanner_next(struct Vec0Scanner *scanner, struct Vec0Token *out) {
  int rc = vec0_token_next(scanner->start, scanner->end, out);
  if (rc == VEC0_TOKEN_RESULT_SOME) {
    scanner->start = out->end;
  }
  return rc;
}

int vec0_parse_table_option(const char *source, int source_length,
                            char **out_key, int *out_key_length,
                            char **out_value, int *out_value_length) {
  int rc;
  struct Vec0Scanner scanner;
  struct Vec0Token token;
  char *key;
  char *value;
  int keyLength, valueLength;

  vec0_scanner_init(&scanner, source, source_length);

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  key = token.start;
  keyLength = token.end - token.start;

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME && token.token_type != TOKEN_TYPE_EQ) {
    return SQLITE_EMPTY;
  }

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      !((token.token_type == TOKEN_TYPE_IDENTIFIER) ||
        (token.token_type == TOKEN_TYPE_DIGIT))) {
    return SQLITE_EMPTY;
  }
  value = token.start;
  valueLength = token.end - token.start;

  rc = vec0_scanner_next(&scanner, &token);
  if (rc == VEC0_TOKEN_RESULT_EOF) {
    *out_key = key;
    *out_key_length = keyLength;
    *out_value = value;
    *out_value_length = valueLength;
    return SQLITE_OK;
  }
  return SQLITE_ERROR;
}
/**
 * @brief Parse an argv[i] entry of a vec0 virtual table definition, and see if
 * it's a PRIMARY KEY definition.
 *
 * @param source: argv[i] source string
 * @param source_length: length of the source string
 * @param out_column_name: If it is a PK, the output column name. Same lifetime
 * as source, points to specific char *
 * @param out_column_name_length: Length of out_column_name in bytes
 * @param out_column_type: SQLITE_TEXT or SQLITE_INTEGER.
 * @return int: SQLITE_EMPTY if not a PK, SQLITE_OK if it is.
 */
int parse_primary_key_definition(const char *source, int source_length,
                                 char **out_column_name,
                                 int *out_column_name_length,
                                 int *out_column_type) {
  struct Vec0Scanner scanner;
  struct Vec0Token token;
  char *column_name;
  int column_name_length;
  int column_type;
  vec0_scanner_init(&scanner, source, source_length);

  // Check first token is identifier, will be the column name
  int rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }

  column_name = token.start;
  column_name_length = token.end - token.start;

  // Check the next token matches "text" or "integer", as column type
  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  if (sqlite3_strnicmp(token.start, "text", token.end - token.start) == 0) {
    column_type = SQLITE_TEXT;
  } else if (sqlite3_strnicmp(token.start, "int", token.end - token.start) ==
                 0 ||
             sqlite3_strnicmp(token.start, "integer",
                              token.end - token.start) == 0) {
    column_type = SQLITE_INTEGER;
  } else {
    return SQLITE_EMPTY;
  }

  // Check the next token is identifier and matches "primary"
  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  if (sqlite3_strnicmp(token.start, "primary", token.end - token.start) != 0) {
    return SQLITE_EMPTY;
  }

  // Check the next token is identifier and matches "key"
  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_EMPTY;
  }
  if (sqlite3_strnicmp(token.start, "key", token.end - token.start) != 0) {
    return SQLITE_EMPTY;
  }

  *out_column_name = column_name;
  *out_column_name_length = column_name_length;
  *out_column_type = column_type;

  return SQLITE_OK;
}

enum Vec0DistanceMetrics {
  VEC0_DISTANCE_METRIC_L2 = 1,
  VEC0_DISTANCE_METRIC_COSINE = 2,
};

struct VectorColumnDefinition {
  char *name;
  int name_length;
  size_t dimensions;
  enum VectorElementType element_type;
  enum Vec0DistanceMetrics distance_metric;
};

size_t vector_column_byte_size(struct VectorColumnDefinition column) {
  switch (column.element_type) {
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32:
    return column.dimensions * sizeof(f32);
  case SQLITE_VEC_ELEMENT_TYPE_INT8:
    return column.dimensions * sizeof(i8);
  case SQLITE_VEC_ELEMENT_TYPE_BIT:
    return column.dimensions / CHAR_BIT;
  }
}

int parse_vector_column(const char *source, int source_length,
                        struct VectorColumnDefinition *column_def) {
  // parses a vector column definition like so:
  // "abc float[123]", "abc_123 bit[1234]", eetc.
  struct Vec0Scanner scanner;
  struct Vec0Token token;

  vec0_scanner_init(&scanner, source, source_length);

  int rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_ERROR;
  }

  column_def->name = token.start;
  column_def->name_length = token.end - token.start;
  column_def->distance_metric = VEC0_DISTANCE_METRIC_L2;

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != TOKEN_TYPE_IDENTIFIER) {
    return SQLITE_ERROR;
  }
  if (sqlite3_strnicmp(token.start, "float", token.end - token.start) == 0 ||
      sqlite3_strnicmp(token.start, "f32", token.end - token.start) == 0) {
    column_def->element_type = SQLITE_VEC_ELEMENT_TYPE_FLOAT32;
  } else if (sqlite3_strnicmp(token.start, "int8", token.end - token.start) ==
                 0 ||
             sqlite3_strnicmp(token.start, "i8", token.end - token.start) ==
                 0) {
    column_def->element_type = SQLITE_VEC_ELEMENT_TYPE_INT8;
  } else if (sqlite3_strnicmp(token.start, "bit", token.end - token.start) ==
             0) {
    column_def->element_type = SQLITE_VEC_ELEMENT_TYPE_BIT;
  } else {
    return SQLITE_ERROR;
  }

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME && token.token_type != TOKEN_TYPE_LBRACKET) {
    return SQLITE_ERROR;
  }

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME && token.token_type != TOKEN_TYPE_DIGIT) {
    return SQLITE_ERROR;
  }
  column_def->dimensions = atoi(token.start);

  rc = vec0_scanner_next(&scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME && token.token_type != TOKEN_TYPE_RBRACKET) {
    return SQLITE_ERROR;
  }

  // any other tokens left should be column-level options , ex `key=value`
  // TODO make sure options are defined only once. ex `distance_metric=L2
  // distance_metric=cosine` should error
  while (1) {
    rc = vec0_scanner_next(&scanner, &token);
    if (rc == VEC0_TOKEN_RESULT_EOF) {
      return SQLITE_OK;
    }

    if (rc != VEC0_TOKEN_RESULT_SOME &&
        token.token_type != TOKEN_TYPE_IDENTIFIER) {
      return SQLITE_ERROR;
    }

    char *key = token.start;
    int keyLength = token.end - token.start;

    if (sqlite3_strnicmp(key, "distance_metric", keyLength) == 0) {

      if (column_def->element_type == SQLITE_VEC_ELEMENT_TYPE_BIT) {
        return SQLITE_ERROR;
      }

      rc = vec0_scanner_next(&scanner, &token);
      if (rc != VEC0_TOKEN_RESULT_SOME && token.token_type != TOKEN_TYPE_EQ) {
        return SQLITE_ERROR;
      }

      rc = vec0_scanner_next(&scanner, &token);
      if (rc != VEC0_TOKEN_RESULT_SOME &&
          token.token_type != TOKEN_TYPE_IDENTIFIER) {
        return SQLITE_ERROR;
      }

      char *value = token.start;
      int valueLength = token.end - token.start;
      if (sqlite3_strnicmp(value, "l2", valueLength) == 0) {
        column_def->distance_metric = VEC0_DISTANCE_METRIC_L2;
      } else if (sqlite3_strnicmp(value, "cosine", valueLength) == 0) {
        column_def->distance_metric = VEC0_DISTANCE_METRIC_COSINE;
      } else {
        return SQLITE_ERROR;
      }
    }
    // unknown option key
    else {
      return SQLITE_ERROR;
    }
  }
}

#pragma region vec_each table function

typedef struct vec_each_vtab vec_each_vtab;
struct vec_each_vtab {
  sqlite3_vtab base;
};

typedef struct vec_each_cursor vec_each_cursor;
struct vec_each_cursor {
  sqlite3_vtab_cursor base;
  i64 iRowid;
  enum VectorElementType vector_type;
  void *vector;
  size_t dimensions;
  vector_cleanup cleanup;
};

static int vec_eachConnect(sqlite3 *db, void *pAux, int argc,
                           const char *const *argv, sqlite3_vtab **ppVtab,
                           char **pzErr) {
  UNUSED_PARAMETER(pAux);
  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);
  UNUSED_PARAMETER(pzErr); // TODO use
  vec_each_vtab *pNew;
  int rc;

  rc = sqlite3_declare_vtab(db, "CREATE TABLE x(value, vector hidden)");
#define VEC_EACH_COLUMN_VALUE 0
#define VEC_EACH_COLUMN_VECTOR 1
  if (rc == SQLITE_OK) {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
      return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  return rc;
}

static int vec_eachDisconnect(sqlite3_vtab *pVtab) {
  vec_each_vtab *p = (vec_each_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int vec_eachOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  UNUSED_PARAMETER(p);
  vec_each_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int vec_eachClose(sqlite3_vtab_cursor *cur) {
  vec_each_cursor *pCur = (vec_each_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int vec_eachBestIndex(sqlite3_vtab *pVTab,
                             sqlite3_index_info *pIdxInfo) {
  int hasVector;
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    // printf("i=%d iColumn=%d, op=%d, usable=%d\n", i, pCons->iColumn,
    // pCons->op, pCons->usable);
    switch (pCons->iColumn) {
    case VEC_EACH_COLUMN_VECTOR: {
      if (pCons->op == SQLITE_INDEX_CONSTRAINT_EQ && pCons->usable) {
        hasVector = 1;
        pIdxInfo->aConstraintUsage[i].argvIndex = 1;
        pIdxInfo->aConstraintUsage[i].omit = 1;
      }
      break;
    }
    }
  }
  if (!hasVector) {
    pVTab->zErrMsg = sqlite3_mprintf("vector argument is required");
    return SQLITE_ERROR;
  }

  pIdxInfo->estimatedCost = (double)100000;
  pIdxInfo->estimatedRows = 100000;

  return SQLITE_OK;
}

static int vec_eachFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                          const char *idxStr, int argc, sqlite3_value **argv) {
  UNUSED_PARAMETER(idxNum);
  UNUSED_PARAMETER(idxStr);
  todo_assert(argc == 1);
  vec_each_cursor *pCur = (vec_each_cursor *)pVtabCursor;

  char *pzErrMsg;
  int rc = vector_from_value(argv[0], &pCur->vector, &pCur->dimensions,
                             &pCur->vector_type, &pCur->cleanup, &pzErrMsg);
  if (rc != SQLITE_OK) {
    return SQLITE_ERROR;
  }
  pCur->iRowid = 0;
  return SQLITE_OK;
}

static int vec_eachRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  vec_each_cursor *pCur = (vec_each_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int vec_eachEof(sqlite3_vtab_cursor *cur) {
  vec_each_cursor *pCur = (vec_each_cursor *)cur;
  return pCur->iRowid >= (i64)pCur->dimensions;
}

static int vec_eachNext(sqlite3_vtab_cursor *cur) {
  vec_each_cursor *pCur = (vec_each_cursor *)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

static int vec_eachColumn(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                          int i) {
  vec_each_cursor *pCur = (vec_each_cursor *)cur;
  switch (i) {
  case VEC_EACH_COLUMN_VALUE:
    switch (pCur->vector_type) {
    case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
      sqlite3_result_double(context, ((f32 *)pCur->vector)[pCur->iRowid]);
      break;
    }
    case SQLITE_VEC_ELEMENT_TYPE_BIT: {
      u8 x = ((u8 *)pCur->vector)[pCur->iRowid / CHAR_BIT];
      sqlite3_result_int(context,
                         (x & (0b10000000 >> ((pCur->iRowid % CHAR_BIT)))) > 0);
      break;
    }
    case SQLITE_VEC_ELEMENT_TYPE_INT8: {
      sqlite3_result_int(context, ((i8 *)pCur->vector)[pCur->iRowid]);
      break;
    }
    }

    break;
  }
  return SQLITE_OK;
}

static sqlite3_module vec_eachModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ vec_eachConnect,
    /* xBestIndex  */ vec_eachBestIndex,
    /* xDisconnect */ vec_eachDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ vec_eachOpen,
    /* xClose      */ vec_eachClose,
    /* xFilter     */ vec_eachFilter,
    /* xNext       */ vec_eachNext,
    /* xEof        */ vec_eachEof,
    /* xColumn     */ vec_eachColumn,
    /* xRowid      */ vec_eachRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0,
    #if SQLITE_VERSION_NUMBER >= 3440000
    /* xIntegrity  */ 0
    #endif
    };

#pragma endregion

#pragma region vec_npy_each table function

static unsigned char NPY_MAGIC[6] = "\x93NUMPY";

enum NpyTokenType {
  NPY_TOKEN_TYPE_IDENTIFIER,
  NPY_TOKEN_TYPE_NUMBER,
  NPY_TOKEN_TYPE_LPAREN,
  NPY_TOKEN_TYPE_RPAREN,
  NPY_TOKEN_TYPE_LBRACE,
  NPY_TOKEN_TYPE_RBRACE,
  NPY_TOKEN_TYPE_COLON,
  NPY_TOKEN_TYPE_COMMA,
  NPY_TOKEN_TYPE_STRING,
  NPY_TOKEN_TYPE_FALSE,
};

struct NpyToken {
  enum NpyTokenType token_type;
  unsigned char *start;
  unsigned char *end;
};

int npy_token_next(unsigned char *start, unsigned char *end,
                   struct NpyToken *out) {
  unsigned char *ptr = start;
  while (ptr < end) {
    unsigned char curr = *ptr;
    if (is_whitespace(curr)) {
      ptr++;
      continue;
    } else if (curr == '(') {
      out->start = ptr++;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_LPAREN;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == ')') {
      out->start = ptr++;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_RPAREN;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == '{') {
      out->start = ptr++;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_LBRACE;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == '}') {
      out->start = ptr++;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_RBRACE;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == ':') {
      out->start = ptr++;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_COLON;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == ',') {
      out->start = ptr++;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_COMMA;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == '\'') {
      unsigned char *start = ptr;
      ptr++;
      while (ptr < end) {
        if ((*ptr) == '\'') {
          break;
        }
        ptr++;
      }
      if ((*ptr) != '\'') {
        return VEC0_TOKEN_RESULT_ERROR;
      }
      out->start = start;
      out->end = ++ptr;
      out->token_type = NPY_TOKEN_TYPE_STRING;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (curr == 'F' &&
               strncmp((char *)ptr, "False", strlen("False")) == 0) {
      out->start = ptr;
      out->end = (ptr + (int)strlen("False"));
      ptr = out->end;
      out->token_type = NPY_TOKEN_TYPE_FALSE;
      return VEC0_TOKEN_RESULT_SOME;
    } else if (is_digit(curr)) {
      unsigned char *start = ptr;
      while (ptr < end && (is_digit(*ptr))) {
        ptr++;
      }
      out->start = start;
      out->end = ptr;
      out->token_type = NPY_TOKEN_TYPE_NUMBER;
      return VEC0_TOKEN_RESULT_SOME;
    } else {
      return VEC0_TOKEN_RESULT_ERROR;
    }
  }
  return VEC0_TOKEN_RESULT_ERROR;
}

struct NpyScanner {
  unsigned char *start;
  unsigned char *end;
  unsigned char *ptr;
};

void npy_scanner_init(struct NpyScanner *scanner, const unsigned char *source,
                      int source_length) {
  scanner->start = (unsigned char *)source;
  scanner->end = (unsigned char *)source + source_length;
  scanner->ptr = (unsigned char *)source;
}

int npy_scanner_next(struct NpyScanner *scanner, struct NpyToken *out) {
  int rc = npy_token_next(scanner->start, scanner->end, out);
  if (rc == VEC0_TOKEN_RESULT_SOME) {
    scanner->start = out->end;
  }
  return rc;
}

int parse_npy_header(const unsigned char *header, size_t headerLength,
                     enum VectorElementType *out_element_type,
                     int *fortran_order, size_t *numElements,
                     size_t *numDimensions) {

  struct NpyScanner scanner;
  struct NpyToken token;
  int rc;
  npy_scanner_init(&scanner, header, headerLength);

  if (npy_scanner_next(&scanner, &token) != VEC0_TOKEN_RESULT_SOME &&
      token.token_type != NPY_TOKEN_TYPE_LBRACE) {
    return SQLITE_ERROR;
  }
  while (1) {
    rc = npy_scanner_next(&scanner, &token);
    todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
    if (token.token_type == NPY_TOKEN_TYPE_RBRACE) {
      break;
    }
    todo_assert(token.token_type == NPY_TOKEN_TYPE_STRING);
    unsigned char *key = token.start;
    // TODO use this in strncmp()?
    // int keyLength = token.end - token.start;

    rc = npy_scanner_next(&scanner, &token);
    todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
    todo_assert(token.token_type == NPY_TOKEN_TYPE_COLON);

    // TODO: strcmp safe?
    if (strncmp((char *)key, "'descr'", strlen("'descr'")) == 0) {
      rc = npy_scanner_next(&scanner, &token);
      todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
      todo_assert(token.token_type == NPY_TOKEN_TYPE_STRING);
      todo_assert(strncmp((char *)token.start, "'<f4'", strlen("'<f4'")) == 0);
      *out_element_type = SQLITE_VEC_ELEMENT_TYPE_FLOAT32;
    } else if (strncmp((char *)key, "'fortran_order'",
                       strlen("'fortran_order'")) == 0) {
      rc = npy_scanner_next(&scanner, &token);
      todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
      todo_assert(token.token_type == NPY_TOKEN_TYPE_FALSE);
      *fortran_order = 0;
    } else if (strncmp((char *)key, "'shape'", strlen("'shape'")) == 0) {
      // "(xxx, xxx)" OR (xxx,)
      size_t first;
      rc = npy_scanner_next(&scanner, &token);
      todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
      todo_assert(token.token_type == NPY_TOKEN_TYPE_LPAREN);

      rc = npy_scanner_next(&scanner, &token);
      todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
      todo_assert(token.token_type == NPY_TOKEN_TYPE_NUMBER);
      first = strtol((char *)token.start, NULL, 10);

      rc = npy_scanner_next(&scanner, &token);
      todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
      todo_assert(token.token_type == NPY_TOKEN_TYPE_COMMA);

      rc = npy_scanner_next(&scanner, &token);
      todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
      if (token.token_type == NPY_TOKEN_TYPE_NUMBER) {
        *numElements = first;
        *numDimensions = strtol((char *)token.start, NULL, 10);
        rc = npy_scanner_next(&scanner, &token);
        todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
        todo_assert(token.token_type == NPY_TOKEN_TYPE_RPAREN);
      } else if (token.token_type == NPY_TOKEN_TYPE_RPAREN) {
        *numElements = 1;
        *numDimensions = first;
      } else {
        todo("unknown value?");
      }
    } else {
      todo("npy header unknown key");
    }

    rc = npy_scanner_next(&scanner, &token);
    todo_assert(rc == VEC0_TOKEN_RESULT_SOME);
    todo_assert(token.token_type == NPY_TOKEN_TYPE_COMMA);
  }

  return SQLITE_OK;
}

int parse_npy(const unsigned char *buffer, size_t bufferLength, void **data,
              size_t *numElements, size_t *numDimensions,
              enum VectorElementType *element_type) {

  todo_assert(bufferLength > 10);
  for (size_t i = 0; i < sizeof(NPY_MAGIC); i++) {
    todo_assert(NPY_MAGIC[i] == buffer[i]);
  }
  u8 major = buffer[6];
  u8 minor = buffer[7];
  uint16_t headerLength = 0;
  memcpy(&headerLength, &buffer[8], sizeof(uint16_t));

  const unsigned char *header = &buffer[10];

  // printf("npy: headerLength=%zu major=%d minor=%d headerLen=%d\n",
  // bufferLength, major, minor, headerLength);
  size_t totalHeaderLength = sizeof(NPY_MAGIC) + sizeof(major) + sizeof(minor) +
                             sizeof(headerLength) + headerLength;
  size_t dataSize = bufferLength - totalHeaderLength;
  todo_assert(dataSize > 0);

  int fortran_order;

  int rc = parse_npy_header(header, headerLength, element_type, &fortran_order,
                            numElements, numDimensions);
  todo_assert(rc == SQLITE_OK);

  int element_size = 0;
  // TODO bit
  if (*element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
    element_size = sizeof(f32);
  }
  todo_assert((*numElements * *numDimensions * element_size) == dataSize);

  *data = (void *)&buffer[totalHeaderLength];
  return SQLITE_OK;
}

typedef struct vec_npy_each_vtab vec_npy_each_vtab;
struct vec_npy_each_vtab {
  sqlite3_vtab base;
};

typedef enum {
  VEC_NPY_EACH_INPUT_BUFFER,
  VEC_NPY_EACH_INPUT_FILE,
} vec_npy_each_input_type;

typedef struct vec_npy_each_cursor vec_npy_each_cursor;
struct vec_npy_each_cursor {
  sqlite3_vtab_cursor base;
  i64 iRowid;
  // sqlite-vec compatible type of vector
  enum VectorElementType elementType;
  // number of vectors in the npy array
  size_t nElements;
  // number of dimensions each vector has
  size_t nDimensions;
  vec_npy_each_input_type input_type;

  // TODO enum this

  // when input_type == VEC_NPY_EACH_INPUT_BUFFER

  // Buffer containing the vector data, when reading from an in-memory buffer.
  // Size: nElements * nDimensions * element_size
  // Clean up with sqlite3_free() once complete
  void *vector;

  // when input_type == VEC_NPY_EACH_INPUT_FILE

  // Opened npy file, when reading from a file.
  // fclose() when complete.
  FILE *file;
  // an in-memory buffer containing a portion of the npy array.
  // Used for faster reading, instead of calling fread() a lot.
  // Will have a byte-size of fileBufferSize
  void *fileBuffer;
  // size of allocated fileBuffer in bytes
  size_t fileBufferSize;
  // Counter index of the current vector into of fileBuffer to yield.
  // Starts at 0 once fileBuffer is read, and iterates to bufferLength.
  // Resets to 0 once that "buffer" is yielded and a new one is read.
  size_t bufferIndex;
  // Maximum length of the buffer, in terms of number of vectors.
  size_t bufferLength;
  // Size of each element inside the vector.
  // Ex: 4 for floats, ex.
  int elementSize;
  // 0 when there are still more elements to read/yield, 1 when complete.
  int eof;
};

static int vec_npy_eachConnect(sqlite3 *db, void *pAux, int argc,
                               const char *const *argv, sqlite3_vtab **ppVtab,
                               char **pzErr) {
  UNUSED_PARAMETER(pAux);
  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);
  UNUSED_PARAMETER(pzErr); // TODO use
  vec_npy_each_vtab *pNew;
  int rc;

  rc = sqlite3_declare_vtab(db, "CREATE TABLE x(vector, input hidden)");
#define VEC_NPY_EACH_COLUMN_VECTOR 0
#define VEC_NPY_EACH_COLUMN_INPUT 1
  if (rc == SQLITE_OK) {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
      return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
  }
  return rc;
}

static int vec_npy_eachDisconnect(sqlite3_vtab *pVtab) {
  vec_npy_each_vtab *p = (vec_npy_each_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int vec_npy_eachOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  UNUSED_PARAMETER(p);
  vec_npy_each_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int vec_npy_eachClose(sqlite3_vtab_cursor *cur) {
  vec_npy_each_cursor *pCur = (vec_npy_each_cursor *)cur;
  if (pCur->file) {
    fclose(pCur->file);
    pCur->file = NULL;
  }
  if (pCur->fileBuffer) {
    sqlite3_free(pCur->fileBuffer);
    pCur->fileBuffer = NULL;
  }
  if (pCur->vector) {
    // sqlite3_free(pCur->vector);
    pCur->vector = NULL;
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int vec_npy_eachBestIndex(sqlite3_vtab *pVTab,
                                 sqlite3_index_info *pIdxInfo) {
  int hasInput;
  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    const struct sqlite3_index_constraint *pCons = &pIdxInfo->aConstraint[i];
    // printf("i=%d iColumn=%d, op=%d, usable=%d\n", i, pCons->iColumn,
    // pCons->op, pCons->usable);
    switch (pCons->iColumn) {
    case VEC_NPY_EACH_COLUMN_INPUT: {
      if (pCons->op == SQLITE_INDEX_CONSTRAINT_EQ && pCons->usable) {
        hasInput = 1;
        pIdxInfo->aConstraintUsage[i].argvIndex = 1;
        pIdxInfo->aConstraintUsage[i].omit = 1;
      }
      break;
    }
    }
  }
  if (!hasInput) {
    pVTab->zErrMsg = sqlite3_mprintf("input argument is required");
    return SQLITE_ERROR;
  }

  pIdxInfo->estimatedCost = (double)100000;
  pIdxInfo->estimatedRows = 100000;

  return SQLITE_OK;
}

static int vec_npy_eachFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                              const char *idxStr, int argc,
                              sqlite3_value **argv) {
  UNUSED_PARAMETER(idxNum);
  UNUSED_PARAMETER(idxStr);
  todo_assert(argc == 1);
  vec_npy_each_cursor *pCur = (vec_npy_each_cursor *)pVtabCursor;

  if (pCur->file) {
    fclose(pCur->file);
    pCur->file = NULL;
  }
  if (pCur->fileBuffer) {
    sqlite3_free(pCur->fileBuffer);
    pCur->fileBuffer = NULL;
  }
  if (pCur->vector) {
    // sqlite3_free(pCur->vector); TODO don't need to free this??
    pCur->vector = NULL;
  }

  struct VecNpyFile *f = NULL;

  if ((f = sqlite3_value_pointer(argv[0], SQLITE_VEC_NPY_FILE_NAME))) {
    int n;
    FILE *file = fopen(f->path, "r");
    todo_assert(file);

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);

    fseek(file, 0L, SEEK_SET);

    unsigned char header[10];
    n = fread(&header, sizeof(unsigned char), 10, file);
    todo_assert(n == 10);

    for (size_t i = 0; i < countof(NPY_MAGIC); i++) {
      todo_assert(NPY_MAGIC[i] == header[i]);
    }
    u8 major = header[6];
    u8 minor = header[7];

    uint16_t headerLength = 0;
    memcpy(&headerLength, &header[8], sizeof(uint16_t));

    size_t totalHeaderLength = sizeof(NPY_MAGIC) + sizeof(major) +
                               sizeof(minor) + sizeof(headerLength) +
                               headerLength;
    size_t dataSize = fileSize - totalHeaderLength;
    todo_assert(dataSize > 0);

    unsigned char *headerX = sqlite3_malloc(headerLength);
    todo_assert(headerX);
    n = fread(headerX, sizeof(char), headerLength, file);
    todo_assert(n == headerLength);

    int fortran_order;
    enum VectorElementType element_type;
    size_t numElements;
    size_t numDimensions;
    int rc = parse_npy_header(headerX, headerLength, &element_type,
                              &fortran_order, &numElements, &numDimensions);
    sqlite3_free(headerX);
    todo_assert(rc == SQLITE_OK);

    int element_size = 0;
    if (element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
      element_size = sizeof(f32);
    } else {
      todo("non-f32 numpy array");
    }

    todo_assert((numElements * numDimensions * element_size) == dataSize);

    pCur->bufferIndex = 0;
    pCur->bufferLength = 1024;
    pCur->elementSize = element_size;
    pCur->elementType = element_type;
    pCur->nElements = numElements;
    pCur->nDimensions = numDimensions;
    pCur->fileBufferSize = numDimensions * element_size * pCur->bufferLength;
    pCur->fileBuffer = sqlite3_malloc(pCur->fileBufferSize);
    todo_assert(pCur->fileBuffer);
    pCur->input_type = VEC_NPY_EACH_INPUT_FILE;
    n = fread(pCur->fileBuffer, 1, pCur->fileBufferSize, file);
    todo_assert((size_t)n == pCur->fileBufferSize); // TODO may be smaller

    pCur->eof = 0;
    pCur->file = file;

  } else {

    const unsigned char *input = sqlite3_value_blob(argv[0]);
    size_t inputLength = sqlite3_value_bytes(argv[0]);
    int rc;
    void *data;
    size_t numElements;
    size_t numDimensions;
    enum VectorElementType element_type;

    rc = parse_npy(input, inputLength, &data, &numElements, &numDimensions,
                   &element_type);
    todo_assert(rc == SQLITE_OK);

    pCur->vector = data;
    pCur->elementType = element_type;
    pCur->nElements = numElements;
    pCur->nDimensions = numDimensions;
    pCur->input_type = VEC_NPY_EACH_INPUT_BUFFER;
  }

  pCur->iRowid = 0;
  return SQLITE_OK;
}

static int vec_npy_eachRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  vec_npy_each_cursor *pCur = (vec_npy_each_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int vec_npy_eachEof(sqlite3_vtab_cursor *cur) {
  vec_npy_each_cursor *pCur = (vec_npy_each_cursor *)cur;
  if (pCur->input_type == VEC_NPY_EACH_INPUT_BUFFER) {
    return (size_t)pCur->iRowid >= pCur->nElements;
  }
  return pCur->eof;
}

static int vec_npy_eachNext(sqlite3_vtab_cursor *cur) {
  vec_npy_each_cursor *pCur = (vec_npy_each_cursor *)cur;
  pCur->iRowid++;
  if (pCur->input_type == VEC_NPY_EACH_INPUT_FILE) {
    pCur->bufferIndex++;
    if (pCur->bufferIndex >= pCur->bufferLength) {
      int n = fread(pCur->fileBuffer, 1, pCur->fileBufferSize, pCur->file);
      if (!n) {
        pCur->eof = 1;
      }
      pCur->bufferIndex = 0;
      pCur->bufferLength = n / pCur->nDimensions / pCur->elementSize;
    }
  }
  return SQLITE_OK;
}

static int vec_npy_eachColumn(sqlite3_vtab_cursor *cur,
                              sqlite3_context *context, int i) {
  vec_npy_each_cursor *pCur = (vec_npy_each_cursor *)cur;
  switch (i) {
  case VEC_NPY_EACH_COLUMN_VECTOR: {
    if (pCur->input_type == VEC_NPY_EACH_INPUT_BUFFER) {
      switch (pCur->elementType) {
      case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
        sqlite3_result_blob(
            context,
            &pCur->vector[pCur->iRowid * pCur->nDimensions * sizeof(f32)],
            pCur->nDimensions * sizeof(f32), SQLITE_STATIC);
        break;
      }
      case SQLITE_VEC_ELEMENT_TYPE_INT8:
      case SQLITE_VEC_ELEMENT_TYPE_BIT: {
        todo("bit array npy column");
        break;
      }
      }
    } else if (pCur->input_type == VEC_NPY_EACH_INPUT_FILE) {
      switch (pCur->elementType) {
      case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
        sqlite3_result_blob(context,
                            &pCur->fileBuffer[pCur->bufferIndex *
                                              pCur->nDimensions * sizeof(f32)],
                            pCur->nDimensions * sizeof(f32), SQLITE_TRANSIENT);
        break;
      }
      case SQLITE_VEC_ELEMENT_TYPE_INT8:
      case SQLITE_VEC_ELEMENT_TYPE_BIT: {
        todo("bit array npy column");
        break;
      }
      }
    }

    break;
  }
  }
  return SQLITE_OK;
}

static sqlite3_module vec_npy_eachModule = {
    /* iVersion    */ 0,
    /* xCreate     */ 0,
    /* xConnect    */ vec_npy_eachConnect,
    /* xBestIndex  */ vec_npy_eachBestIndex,
    /* xDisconnect */ vec_npy_eachDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ vec_npy_eachOpen,
    /* xClose      */ vec_npy_eachClose,
    /* xFilter     */ vec_npy_eachFilter,
    /* xNext       */ vec_npy_eachNext,
    /* xEof        */ vec_npy_eachEof,
    /* xColumn     */ vec_npy_eachColumn,
    /* xRowid      */ vec_npy_eachRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0,
    #if SQLITE_VERSION_NUMBER >= 3440000
    /* xIntegrity  */ 0,
    #endif
    };

#pragma endregion

#pragma region vec0 virtual table

#define VEC0_COLUMN_ID 0
#define VEC0_COLUMN_VECTORN_START 1
#define VEC0_COLUMN_OFFSET_DISTANCE 1
#define VEC0_COLUMN_OFFSET_K 2

#define VEC0_SHADOW_CHUNKS_NAME "\"%w\".\"%w_chunks\""
/// 1) schema, 2) original vtab table name
#define VEC0_SHADOW_CHUNKS_CREATE                                              \
  "CREATE TABLE " VEC0_SHADOW_CHUNKS_NAME "("                                  \
  "chunk_id INTEGER PRIMARY KEY AUTOINCREMENT,"                                \
  "size INTEGER NOT NULL,"                                                     \
  "validity BLOB NOT NULL,"                                                    \
  "rowids BLOB NOT NULL"                                                       \
  ");"

#define VEC0_SHADOW_ROWIDS_NAME "\"%w\".\"%w_rowids\""
/// 1) schema, 2) original vtab table name
#define VEC0_SHADOW_ROWIDS_CREATE_BASIC                                        \
  "CREATE TABLE " VEC0_SHADOW_ROWIDS_NAME "("                                  \
  "rowid INTEGER PRIMARY KEY AUTOINCREMENT,"                                   \
  "id,"                                                                        \
  "chunk_id INTEGER,"                                                          \
  "chunk_offset INTEGER"                                                       \
  ");"

// vec0 tables with a text primary keys are still backed by int64 primary keys,
// since a fixed-length rowid is required for vec0 chunks. But we add a new 'id
// text unique' column to emulate a text primary key interface.
#define VEC0_SHADOW_ROWIDS_CREATE_PK_TEXT                                      \
  "CREATE TABLE " VEC0_SHADOW_ROWIDS_NAME "("                                  \
  "rowid INTEGER PRIMARY KEY AUTOINCREMENT,"                                   \
  "id TEXT UNIQUE NOT NULL,"                                                   \
  "chunk_id INTEGER,"                                                          \
  "chunk_offset INTEGER"                                                       \
  ");"

/// 1) schema, 2) original vtab table name
#define VEC0_SHADOW_VECTOR_N_NAME "\"%w\".\"%w_vector_chunks%02d\""

/// 1) schema, 2) original vtab table name
#define VEC0_SHADOW_VECTOR_N_CREATE                                            \
  "CREATE TABLE " VEC0_SHADOW_VECTOR_N_NAME "("                                \
  "rowid PRIMARY KEY,"                                                         \
  "vectors BLOB NOT NULL"                                                      \
  ");"

typedef struct vec0_vtab vec0_vtab;

#define VEC0_MAX_VECTOR_COLUMNS 16
struct vec0_vtab {
  sqlite3_vtab base;

  // the SQLite connection of the host database
  sqlite3 *db;

  // True if the primary key of the vec0 table has a column type TEXT.
  // Will change the schema of the _rowids table, and insert/query logic.
  int pkIsText;

  // Name of the schema the table exists on.
  // Must be freed with sqlite3_free()
  char *schemaName;

  // Name of the table the table exists on.
  // Must be freed with sqlite3_free()
  char *tableName;

  // Name of the _rowids shadow table.
  // Must be freed with sqlite3_free()
  char *shadowRowidsName;

  // Name of the _chunks shadow table.
  // Must be freed with sqlite3_free()
  char *shadowChunksName;

  // Name of all the vector chunk shadow tables.
  // Only the first numVectorColumns entries will be available.
  // The first numVectorColumns entries must be freed with sqlite3_free()
  char *shadowVectorChunksNames[VEC0_MAX_VECTOR_COLUMNS];

  struct VectorColumnDefinition vector_columns[VEC0_MAX_VECTOR_COLUMNS];

  // number of defined numVectorColumns columns.
  int numVectorColumns;

  int chunk_size;

  // select latest chunk from _chunks, getting chunk_id
  sqlite3_stmt *stmtLatestChunk;

  /**
   * Statement to insert a row into the _rowids table, with a rowid.
   * Parameters:
   *    1: int64, rowid to insert
   * Result columns: none
   * SQL: "INSERT INTO _rowids(rowid) VALUES (?)"
   *
   * Must be cleaned up with sqlite3_finalize().
   */
  sqlite3_stmt *stmtRowidsInsertRowid;

  /**
   * Statement to insert a row into the _rowids table, with an id.
   * The id column isn't a tradition primary key, but instead a unique
   * column to handle "text primary key" vec0 tables. The true int64 rowid
   * can be retrieved after inserting with sqlite3_last_rowid().
   *
   * Parameters:
   *    1: text or null, id to insert
   * Result columns: none
   *
   * Must be cleaned up with sqlite3_finalize().
   */
  sqlite3_stmt *stmtRowidsInsertId;

  /**
   * Statement to update the "position" columns chunk_id and chunk_offset for
   * a given _rowids row. Used when the "next available" chunk position is found
   * for a vector.
   *
   * Parameters:
   *    1: int64, chunk_id value
   *    2: int64, chunk_offset value
   *    3: int64, rowid value
   * Result columns: none
   *
   * Must be cleaned up with sqlite3_finalize().
   */
  sqlite3_stmt *stmtRowidsUpdatePosition;

  /**
   * Statement to quickly find the chunk_id + chunk_offset of a given row.
   * Parameters:
   *  1: rowid of the row/vector to lookup
   * Result columns:
   *  0: chunk_id (i64)
   *  1: chunk_offset (i64)
   * SQL: "SELECT chunk_id, chunk_offset FROM _rowids WHERE rowid = ?""
   *
   * Must be cleaned up with sqlite3_finalize().
   */
  sqlite3_stmt *stmtRowidsGetChunkPosition;

  /**
   * Cached SQLite BLOBs for every possible vector column for the table.
   * Defined for all vectors up to index numVectorColumns (always <=
   * VEC0_MAX_VECTOR_COLUMNS).
   *
   * Defined from:
   *  db: p->schemaName
   *  table: p->shadowVectorChunksNames[i]
   *  column: "vectors"
   *
   * Opened at vec0_init() time.
   * Must be cleaned up with sqlite3_blob_close() at xDisconnect.
   *
   */
  sqlite3_blob *vectorBlobs[VEC0_MAX_VECTOR_COLUMNS];
};

int vec0_column_distance_idx(vec0_vtab *pVtab) {
  return VEC0_COLUMN_VECTORN_START + (pVtab->numVectorColumns - 1) +
         VEC0_COLUMN_OFFSET_DISTANCE;
}
int vec0_column_k_idx(vec0_vtab *pVtab) {
  return VEC0_COLUMN_VECTORN_START + (pVtab->numVectorColumns - 1) +
         VEC0_COLUMN_OFFSET_K;
}

/**
 * Returns 1 if the given column-based index is a valid vector column,
 * 0 otherwise.
 */
int vec0_column_idx_is_vector(vec0_vtab *pVtab, int column_idx) {
  return column_idx >= VEC0_COLUMN_VECTORN_START &&
         column_idx <= (VEC0_COLUMN_VECTORN_START + pVtab->numVectorColumns -
                        1); // TODO is -1 necessary here?
}

/**
 * Returns the vector index of the given vector column index.
 * ONLY call if validated with vec0_column_idx_is_vector before
 */
int vec0_column_idx_to_vector_idx(vec0_vtab *pVtab, int column_idx) {
  UNUSED_PARAMETER(pVtab);
  return column_idx - VEC0_COLUMN_VECTORN_START;
}

/**
 * @brief Return the id value from the _rowids table where _rowids.rowid =
 * rowid.
 *
 * @param pVtab: vec0 table to query
 * @param rowid: rowid of the row to query.
 * @param out: A dup'ed sqlite3_value of the id column. Might be null.
 *                         Must be cleaned up with sqlite3_value_free().
 * @returns SQLITE_OK on success, error code on failure
 */
int vec0_get_id_value_from_rowid(vec0_vtab *pVtab, i64 rowid,
                                 sqlite3_value **out) {
  // TODO different stmt than stmtRowidsGetChunkPosition?
  // TODO return rc instead
  sqlite3_reset(pVtab->stmtRowidsGetChunkPosition);
  sqlite3_clear_bindings(pVtab->stmtRowidsGetChunkPosition);
  sqlite3_bind_int64(pVtab->stmtRowidsGetChunkPosition, 1, rowid);
  int rc = sqlite3_step(pVtab->stmtRowidsGetChunkPosition);
  if (rc == SQLITE_ROW) {
    return SQLITE_ERROR;
  }
  sqlite3_value *value =
      sqlite3_column_value(pVtab->stmtRowidsGetChunkPosition, 0);
  *out = sqlite3_value_dup(value);
  return SQLITE_OK;
}

// TODO make sure callees use the return value of this function
int vec0_result_id(vec0_vtab *p, sqlite3_context *context, i64 rowid) {
  if (!p->pkIsText) {
    sqlite3_result_int64(context, rowid);
    return SQLITE_OK;
  }
  sqlite3_value *valueId;
  int rc = vec0_get_id_value_from_rowid(p, rowid, &valueId);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (!valueId) {
    sqlite3_result_error_nomem(context);
  } else {
    sqlite3_result_value(context, valueId);
    sqlite3_value_free(valueId);
  }
  return SQLITE_OK;
}

/**
 * @brief
 *
 * @param pVtab: virtual table to query
 * @param rowid: row to lookup
 * @param vector_column_idx: which vector column to query
 * @param outVector: Output pointer to the vector buffer.
 *                    Must be sqlite3_free()'ed.
 * @param outVectorSize: Pointer to a int where the size of outVector
 *                       will be stored.
 * @return int SQLITE_OK on success.
 */
int vec0_get_vector_data(vec0_vtab *pVtab, i64 rowid, int vector_column_idx,
                         void **outVector, int *outVectorSize) {
  todo_assert((vector_column_idx >= 0) &&
              (vector_column_idx < pVtab->numVectorColumns));

  sqlite3_reset(pVtab->stmtRowidsGetChunkPosition);
  sqlite3_clear_bindings(pVtab->stmtRowidsGetChunkPosition);
  sqlite3_bind_int64(pVtab->stmtRowidsGetChunkPosition, 1, rowid);
  int rc = sqlite3_step(pVtab->stmtRowidsGetChunkPosition);
  todo_assert(rc == SQLITE_ROW);
  i64 chunk_id = sqlite3_column_int64(pVtab->stmtRowidsGetChunkPosition, 1);
  i64 chunk_offset = sqlite3_column_int64(pVtab->stmtRowidsGetChunkPosition, 2);

  rc = sqlite3_blob_reopen(pVtab->vectorBlobs[vector_column_idx], chunk_id);
  todo_assert(rc == SQLITE_OK);
  size_t size =
      vector_column_byte_size(pVtab->vector_columns[vector_column_idx]);
  int blobOffset = chunk_offset * size;

  void *buf = sqlite3_malloc(size);
  todo_assert(buf);
  rc = sqlite3_blob_read(pVtab->vectorBlobs[vector_column_idx], buf, size,
                         blobOffset);
  todo_assert(rc == SQLITE_OK);

  *outVector = buf;
  if (outVectorSize) {
    *outVectorSize = size;
  }
  return SQLITE_OK;
}

/**
 * @brief For the given rowid, found the chunk_id and chunk_offset for that row.
 *
 * @param p: vec0 table
 * @param rowid: rowid of row to lookup
 * @param chunk_id: Output chunk_id of the row, refs _chunks.rowid
 * @param chunk_offset: Output chunk_offset of the row
 * @return int: SQLITE_OK on success, error code on failure
 */
int vec0_get_chunk_position(vec0_vtab *p, i64 rowid, i64 *chunk_id,
                            i64 *chunk_offset) {
  int rc;
  sqlite3_reset(p->stmtRowidsGetChunkPosition);
  sqlite3_clear_bindings(p->stmtRowidsGetChunkPosition);
  sqlite3_bind_int64(p->stmtRowidsGetChunkPosition, 1, rowid);
  rc = sqlite3_step(p->stmtRowidsGetChunkPosition);
  assert(rc == SQLITE_ROW);
  *chunk_id = sqlite3_column_int64(p->stmtRowidsGetChunkPosition, 1);
  *chunk_offset = sqlite3_column_int64(p->stmtRowidsGetChunkPosition, 2);
  rc = sqlite3_step(p->stmtRowidsGetChunkPosition);
  todo_assert(rc == SQLITE_DONE);
  return SQLITE_OK;
}

/**
 * @brief Adds a new chunk for the vec0 table, and the corresponding vector
 * chunks.
 *
 * Inserts a new row into the _chunks table, with blank data, and uses that new
 * rowid to insert new blank rows into _vector_chunksXX tables.
 *
 * @param p: vec0 table to add new chunk
 * @param chunk_rowid: Putput pointer, if not NULL, then will be filled with the
 * new chunk rowid.
 * @return int SQLITE_OK on success, error code otherwise.
 */
int vec0_new_chunk(vec0_vtab *p, i64 *chunk_rowid) {
  int rc;
  char *zSql;
  sqlite3_stmt *stmt;
  i64 rowid;

  // Step 1: Insert a new row in _chunks, capture that new rowid
  zSql = sqlite3_mprintf("INSERT INTO " VEC0_SHADOW_CHUNKS_NAME
                         "(size, validity, rowids) "
                         "VALUES (?, ?, ?);",
                         p->schemaName, p->tableName);
  if(!zSql) {
    return SQLITE_NOMEM;
  }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if(rc != SQLITE_OK) {
    return rc;
  }
#ifdef SQLITE_VEC_THREADSAFE
  sqlite3_mutex_enter(sqlite3_db_mutex(p->db));
#endif
  rc = sqlite3_bind_int64(stmt, 1, p->chunk_size); // size
  if(rc != SQLITE_OK) {
    #ifdef SQLITE_VEC_THREADSAFE
      sqlite3_mutex_leave(sqlite3_db_mutex(p->db));
    #endif
    sqlite3_finalize(stmt);
    return SQLITE_ERROR;
  }
  rc = sqlite3_bind_zeroblob(stmt, 2,
                             p->chunk_size / CHAR_BIT); // validity bitmap
  todo_assert(rc == SQLITE_OK);
  rc = sqlite3_bind_zeroblob(stmt, 3,
                             p->chunk_size * sizeof(i64)); // rowids
  todo_assert(rc == SQLITE_OK);

  rc = sqlite3_step(stmt);
  todo_assert(rc == SQLITE_DONE);
  rowid = sqlite3_last_insert_rowid(p->db);
#ifdef SQLITE_VEC_THREADSAFE
  sqlite3_mutex_leave(sqlite3_db_mutex(p->db));
#endif
  sqlite3_finalize(stmt);

  // Step 2: Create new vector chunks for each vector column, with
  //          that new chunk_rowid.

  for (int i = 0; i < p->numVectorColumns; i++) {

    i64 vectorsSize = 0;
    switch (p->vector_columns[i].element_type) {
    case SQLITE_VEC_ELEMENT_TYPE_FLOAT32:
      vectorsSize =
          p->chunk_size * p->vector_columns[i].dimensions * sizeof(f32);
      break;
    case SQLITE_VEC_ELEMENT_TYPE_INT8:
      vectorsSize =
          p->chunk_size * p->vector_columns[i].dimensions * sizeof(i8);
      break;
    case SQLITE_VEC_ELEMENT_TYPE_BIT:
      vectorsSize =
          ceil(p->chunk_size * p->vector_columns[i].dimensions / CHAR_BIT);
      break;
    }

    zSql = sqlite3_mprintf("INSERT INTO " VEC0_SHADOW_VECTOR_N_NAME
                           "(rowid, vectors)"
                           "VALUES (?, ?)",
                           p->schemaName, p->tableName, i);
    todo_assert(zSql);
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
    sqlite3_free(zSql);
    todo_assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 1, rowid);
    todo_assert(rc == SQLITE_OK);

    rc = sqlite3_bind_zeroblob64(stmt, 2, vectorsSize);
    todo_assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    todo_assert(rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
  }

  if (chunk_rowid) {
    *chunk_rowid = rowid;
  }

  return SQLITE_OK;
}

// Possible query plans for xBestIndex on vec0 tables.
typedef enum {
  // Full scan, every row is queried.
  SQLITE_VEC0_QUERYPLAN_FULLSCAN,
  // A single row is queried by rowid/id
  SQLITE_VEC0_QUERYPLAN_POINT,
  // A KNN-style query is made on a specific vector column.
  // Requires 1) a MATCH/compatible distance contraint on
  // a single vector column, 2) ORDER BY distance, and 3)
  // either a 'LIMIT ?' or 'k=?' contraint
  SQLITE_VEC0_QUERYPLAN_KNN,
} vec0_query_plan;

struct vec0_query_fullscan_data {
  sqlite3_stmt *rowids_stmt;
  i8 done;
};
int vec0_query_fullscan_data_clear(
    struct vec0_query_fullscan_data *fullscan_data) {
  int rc;
  if (fullscan_data->rowids_stmt) {
    rc = sqlite3_finalize(fullscan_data->rowids_stmt);
    todo_assert(rc == SQLITE_OK);
    fullscan_data->rowids_stmt = NULL;
  }
  return SQLITE_OK;
}

struct vec0_query_knn_data {
  i64 k;
  // Array of rowids of size k. Must be freed with sqlite3_free().
  i64 *rowids;
  // Array of distances of size k. Must be freed with sqlite3_free().
  f32 *distances;
  i64 current_idx;
};
int vec0_query_knn_data_clear(struct vec0_query_knn_data *knn_data) {
  if (knn_data->rowids) {
    sqlite3_free(knn_data->rowids);
    knn_data->rowids = NULL;
  }
  if (knn_data->distances) {
    sqlite3_free(knn_data->distances);
    knn_data->distances = NULL;
  }
  return SQLITE_OK;
}

struct vec0_query_point_data {
  i64 rowid;
  void *vectors[VEC0_MAX_VECTOR_COLUMNS];
  int done;
};
void vec0_query_point_data_clear(struct vec0_query_point_data *point_data) {
  for (int i = 0; i < VEC0_MAX_VECTOR_COLUMNS; i++) {
    sqlite3_free(point_data->vectors[i]);
    point_data->vectors[i] = NULL;
  }
}

typedef struct vec0_cursor vec0_cursor;
struct vec0_cursor {
  sqlite3_vtab_cursor base;

  vec0_query_plan query_plan;
  struct vec0_query_fullscan_data *fullscan_data;
  struct vec0_query_knn_data *knn_data;
  struct vec0_query_point_data *point_data;
};

#define SET_VTAB_ERROR(msg)                                                    \
  do {                                                                         \
    sqlite3_free(pVTab->zErrMsg);                                              \
    pVTab->zErrMsg = sqlite3_mprintf("%s", msg);                               \
  } while (0)
#define SET_VTAB_CURSOR_ERROR(msg)                                             \
  do {                                                                         \
    sqlite3_free(pVtabCursor->pVtab->zErrMsg);                                 \
    pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf("%s", msg);                  \
  } while (0)

static int vec0_init(sqlite3 *db, void *pAux, int argc, const char *const *argv,
                     sqlite3_vtab **ppVtab, char **pzErr, bool isCreate) {
  UNUSED_PARAMETER(pAux);
  UNUSED_PARAMETER(pzErr); // TODO use!
  vec0_vtab *pNew;
  int rc;
  const char *zSql;

  pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew == 0)
    return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));
  *ppVtab = (sqlite3_vtab *)pNew;

  int chunk_size = -1;
  int numVectorColumns = 0;

  // track if a "primary key" column is defined
  char *pkColumnName = NULL;
  int pkColumnNameLength;
  int pkColumnType;

  for (int i = 3; i < argc; i++) {
    todo_assert(numVectorColumns <= VEC0_MAX_VECTOR_COLUMNS);
    int rc = parse_vector_column(argv[i], strlen(argv[i]),
                                 &pNew->vector_columns[numVectorColumns]);
    if (rc == SQLITE_OK) {
      todo_assert(rc == SQLITE_OK);
      todo_assert(pNew->vector_columns[numVectorColumns].dimensions > 0);
      pNew->vector_columns[numVectorColumns].name = sqlite3_mprintf(
          "%.*s", pNew->vector_columns[numVectorColumns].name_length,
          pNew->vector_columns[numVectorColumns].name);
      assert(pNew->vector_columns[numVectorColumns].name);
      numVectorColumns++;
      continue;
    }

    char *cName = NULL;
    int cNameLength;
    int cType;
    rc = parse_primary_key_definition(argv[i], strlen(argv[i]), &cName,
                                      &cNameLength, &cType);
    if (rc == SQLITE_OK) {
      todo_assert(!pkColumnName);
      pkColumnName = cName;
      pkColumnNameLength = cNameLength;
      pkColumnType = cType;
      continue;
    }
    char *key;
    char *value;
    int keyLength, valueLength;
    rc = vec0_parse_table_option(argv[i], strlen(argv[i]), &key, &keyLength,
                                 &value, &valueLength);
    if (rc == SQLITE_OK) {
      if (sqlite3_strnicmp(key, "chunk_size", keyLength) == 0) {
        todo_assert(chunk_size < 0);
        chunk_size = atoi(value);
        if (chunk_size <= 0) {
          todo("chunk_size must be positive");
        }
        if ((chunk_size % 8) != 0) {
          todo("chunk_size must be divisible by 8");
        }
      } else {
        todo("handle unknown table option");
      }
      continue;
    }
    todo("unparseable constructor");
  }

  if (chunk_size < 0) {
    chunk_size = 1024;
  }

  todo_assert(numVectorColumns > 0);
  todo_assert(numVectorColumns <= VEC0_MAX_VECTOR_COLUMNS);

  sqlite3_str *createStr = sqlite3_str_new(NULL);
  sqlite3_str_appendall(createStr, "CREATE TABLE x(");
  if (pkColumnName) {
    sqlite3_str_appendf(createStr, "\"%.*w\" primary key, ", pkColumnNameLength,
                        pkColumnName);
  } else {
    sqlite3_str_appendall(createStr, "rowid, ");
  }
  for (int i = 0; i < numVectorColumns; i++) {
    sqlite3_str_appendf(createStr, "\"%.*w\", ",
                        pNew->vector_columns[i].name_length,
                        pNew->vector_columns[i].name);
  }
  sqlite3_str_appendall(createStr, " distance hidden, k hidden) ");
  if (pkColumnName) {
    sqlite3_str_appendall(createStr, "without rowid ");
  }
  zSql = sqlite3_str_finish(createStr);
  todo_assert(zSql);
  rc = sqlite3_declare_vtab(db, zSql);
  sqlite3_free((void *)zSql);
  if (rc != SQLITE_OK) {
    return rc;
  }

  todo_assert(chunk_size > 0);

  const char *schemaName = argv[1];
  const char *tableName = argv[2];

  pNew->db = db;
  pNew->pkIsText = pkColumnType == SQLITE_TEXT;
  pNew->schemaName = sqlite3_mprintf("%s", schemaName);
  pNew->tableName = sqlite3_mprintf("%s", tableName);
  pNew->shadowRowidsName = sqlite3_mprintf("%s_rowids", tableName);
  pNew->shadowChunksName = sqlite3_mprintf("%s_chunks", tableName);
  pNew->numVectorColumns = numVectorColumns;
  for (int i = 0; i < pNew->numVectorColumns; i++) {
    pNew->shadowVectorChunksNames[i] =
        sqlite3_mprintf("%s_vector_chunks%02d", tableName, i);
  }
  pNew->chunk_size = chunk_size;

  // if xCreate, then create the necessary shadow tables
  if (isCreate) {
    sqlite3_stmt *stmt;
    int rc;
    char *zCreateShadowChunks;
    char *zCreateShadowRowids;

    // create the _chunks shadow table
    zCreateShadowChunks = sqlite3_mprintf(VEC0_SHADOW_CHUNKS_CREATE,
                                          pNew->schemaName, pNew->tableName);
    todo_assert(zCreateShadowChunks);
    rc = sqlite3_prepare_v2(db, zCreateShadowChunks, -1, &stmt, 0);
    sqlite3_free((void *)zCreateShadowChunks);
    todo_assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    todo_assert(rc == SQLITE_DONE);
    sqlite3_finalize(stmt);

    // create the _rowids shadow table
    if (pNew->pkIsText) {
      // adds a "text unique not null" constraint to the id column
      zCreateShadowRowids = sqlite3_mprintf(VEC0_SHADOW_ROWIDS_CREATE_PK_TEXT,
                                            pNew->schemaName, pNew->tableName);
    } else {
      zCreateShadowRowids = sqlite3_mprintf(VEC0_SHADOW_ROWIDS_CREATE_BASIC,
                                            pNew->schemaName, pNew->tableName);
    }
    todo_assert(zCreateShadowRowids);
    rc = sqlite3_prepare_v2(db, zCreateShadowRowids, -1, &stmt, 0);
    sqlite3_free((void *)zCreateShadowRowids);
    todo_assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    todo_assert(rc == SQLITE_DONE);
    sqlite3_finalize(stmt);

    for (int i = 0; i < pNew->numVectorColumns; i++) {
      char *zSql = sqlite3_mprintf(VEC0_SHADOW_VECTOR_N_CREATE,
                                   pNew->schemaName, pNew->tableName, i);
      todo_assert(zSql);
      int rc = sqlite3_prepare_v2(db, zSql, -1, &stmt, 0);
      todo_assert(rc == SQLITE_OK);
      rc = sqlite3_step(stmt);
      todo_assert(rc == SQLITE_DONE);
      sqlite3_finalize(stmt);
      sqlite3_free((void *)zSql);
    }

    rc = vec0_new_chunk(pNew, NULL);
    assert(rc == SQLITE_OK);
  }

  // init stmtLatestChunk
  {
    zSql = sqlite3_mprintf("SELECT max(rowid) FROM " VEC0_SHADOW_CHUNKS_NAME,
                           pNew->schemaName, pNew->tableName);
    todo_assert(zSql);
    rc = sqlite3_prepare_v2(pNew->db, zSql, -1, &pNew->stmtLatestChunk, 0);
    sqlite3_free((void *)zSql);
    todo_assert(rc == SQLITE_OK);
  }

  // init stmtRowidsInsertRowid
  {
    zSql = sqlite3_mprintf("INSERT INTO " VEC0_SHADOW_ROWIDS_NAME "(rowid)"
                           "VALUES (?);",
                           pNew->schemaName, pNew->tableName);
    todo_assert(zSql);
    rc =
        sqlite3_prepare_v2(pNew->db, zSql, -1, &pNew->stmtRowidsInsertRowid, 0);
    sqlite3_free((void *)zSql);
    todo_assert(rc == SQLITE_OK);
  }

  // init stmtRowidsInsertId
  {
    zSql = sqlite3_mprintf("INSERT INTO " VEC0_SHADOW_ROWIDS_NAME "(id)"
                           "VALUES (?);",
                           pNew->schemaName, pNew->tableName);
    todo_assert(zSql);
    rc = sqlite3_prepare_v2(pNew->db, zSql, -1, &pNew->stmtRowidsInsertId, 0);
    sqlite3_free((void *)zSql);
    todo_assert(rc == SQLITE_OK);
  }

  // init stmtRowidsUpdatePosition
  {
    zSql = sqlite3_mprintf(" UPDATE " VEC0_SHADOW_ROWIDS_NAME
                           " SET chunk_id = ?, chunk_offset = ?"
                           " WHERE rowid = ?",
                           pNew->schemaName, pNew->tableName);
    todo_assert(zSql);
    rc = sqlite3_prepare_v2(pNew->db, zSql, -1, &pNew->stmtRowidsUpdatePosition,
                            0);
    sqlite3_free((void *)zSql);
    todo_assert(rc == SQLITE_OK);
  }

  // init stmtRowidsGetChunkPosition
  {
    zSql = sqlite3_mprintf("SELECT id, chunk_id, chunk_offset "
                           "FROM " VEC0_SHADOW_ROWIDS_NAME " WHERE rowid = ?",
                           pNew->schemaName, pNew->tableName);
    todo_assert(zSql);
    rc = sqlite3_prepare_v2(pNew->db, zSql, -1,
                            &pNew->stmtRowidsGetChunkPosition, 0);
    sqlite3_free((void *)zSql);
    todo_assert(rc == SQLITE_OK);
  }

  // init vectorBlobs[..]
  for (int i = 0; i < pNew->numVectorColumns; i++) {
    // TODO this is assuming there's always a chunk with chunk_id = 1. Is that
    // true?
    int rc = sqlite3_blob_open(db, pNew->schemaName,
                               pNew->shadowVectorChunksNames[i], "vectors", 1,
                               0, &pNew->vectorBlobs[i]);
    todo_assert(rc == SQLITE_OK);
  }

  return SQLITE_OK;
}

static int vec0Create(sqlite3 *db, void *pAux, int argc,
                      const char *const *argv, sqlite3_vtab **ppVtab,
                      char **pzErr) {
  return vec0_init(db, pAux, argc, argv, ppVtab, pzErr, true);
}
static int vec0Connect(sqlite3 *db, void *pAux, int argc,
                       const char *const *argv, sqlite3_vtab **ppVtab,
                       char **pzErr) {
  return vec0_init(db, pAux, argc, argv, ppVtab, pzErr, false);
}

static int vec0Disconnect(sqlite3_vtab *pVtab) {
  vec0_vtab *p = (vec0_vtab *)pVtab;
  sqlite3_free(p->schemaName);
  sqlite3_free(p->tableName);
  sqlite3_free(p->shadowChunksName);
  sqlite3_free(p->shadowRowidsName);
  for (int i = 0; i < p->numVectorColumns; i++) {
    sqlite3_free(p->shadowVectorChunksNames[i]);
    sqlite3_blob_close(p->vectorBlobs[i]);
  }
  sqlite3_finalize(p->stmtLatestChunk);
  sqlite3_finalize(p->stmtRowidsInsertRowid);
  sqlite3_finalize(p->stmtRowidsInsertId);
  sqlite3_finalize(p->stmtRowidsUpdatePosition);
  sqlite3_finalize(p->stmtRowidsGetChunkPosition);
  for (int i = 0; i < p->numVectorColumns; i++) {
    sqlite3_free(p->vector_columns[i].name);
    p->vector_columns[i].name = NULL;
  }
  sqlite3_free(p);
  return SQLITE_OK;
}
static int vec0Destroy(sqlite3_vtab *pVtab) {
  vec0_vtab *p = (vec0_vtab *)pVtab;
  sqlite3_stmt *stmt;
  const char *zSql = sqlite3_mprintf("TODO", p->schemaName, p->tableName);
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, 0);
  sqlite3_free((void *)zSql);

  if (rc == SQLITE_OK) {
    // ignore if there's an error?
    sqlite3_step(stmt);
  }

  sqlite3_finalize(stmt);
  vec0Disconnect(pVtab);
  return SQLITE_OK;
}

static int vec0Open(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  UNUSED_PARAMETER(p);
  vec0_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int vec0Close(sqlite3_vtab_cursor *cur) {
  int rc;
  vec0_cursor *pCur = (vec0_cursor *)cur;
  if (pCur->fullscan_data) {
    rc = vec0_query_fullscan_data_clear(pCur->fullscan_data);
    todo_assert(rc == SQLITE_OK);
    sqlite3_free(pCur->fullscan_data);
  }
  if (pCur->knn_data) {
    rc = vec0_query_knn_data_clear(pCur->knn_data);
    todo_assert(rc == SQLITE_OK);
    sqlite3_free(pCur->knn_data);
  }
  if (pCur->point_data) {
    vec0_query_point_data_clear(pCur->point_data);
    sqlite3_free(pCur->point_data);
  }
  sqlite3_free(pCur);
  return SQLITE_OK;
}

#define VEC0_QUERY_PLAN_FULLSCAN "fullscan"
#define VEC0_QUERY_PLAN_POINT "point"
#define VEC0_QUERY_PLAN_KNN "knn"

static int vec0BestIndex(sqlite3_vtab *pVTab, sqlite3_index_info *pIdxInfo) {
  vec0_vtab *p = (vec0_vtab *)pVTab;
  /**
   * Possible query plans are:
   * 1. KNN when:
   *    a) An `MATCH` op on vector column
   *    b) ORDER BY on distance column
   *    c) LIMIT
   *    d) rowid in (...) OPTIONAL
   * 2. Point when:
   *    a) An `EQ` op on rowid column
   * 3. else: fullscan
   *
   */
  int iMatchTerm = -1;
  int iMatchVectorTerm = -1;
  int iLimitTerm = -1;
  int iRowidTerm = -1;
  int iKTerm = -1;
  int iRowidInTerm = -1;

#ifdef SQLITE_VEC_DEBUG
  printf("pIdxInfo->nOrderBy=%d\n", pIdxInfo->nOrderBy);
#endif

  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    u8 vtabIn = 0;
    // sqlite3_vtab_in() was added in SQLite version 3.38 (2022-02-22)
    // ref: https://www.sqlite.org/changes.html#version_3_38_0
    if (sqlite3_libversion_number() >= 3038000) {
      vtabIn = sqlite3_vtab_in(pIdxInfo, i, -1);
    }
#ifdef SQLITE_VEC_DEBUG
    printf("xBestIndex [%d] usable=%d iColumn=%d op=%d vtabin=%d\n", i,
           pIdxInfo->aConstraint[i].usable, pIdxInfo->aConstraint[i].iColumn,
           pIdxInfo->aConstraint[i].op, vtabIn);
#endif
    if (!pIdxInfo->aConstraint[i].usable)
      continue;

    int iColumn = pIdxInfo->aConstraint[i].iColumn;
    int op = pIdxInfo->aConstraint[i].op;
    if (op == SQLITE_INDEX_CONSTRAINT_MATCH &&
        vec0_column_idx_is_vector(p, iColumn)) {
      if (iMatchTerm > -1) {
        // TODO only 1 match operator at a time
        return SQLITE_ERROR;
      }
      iMatchTerm = i;
      iMatchVectorTerm = vec0_column_idx_to_vector_idx(p, iColumn);
    }
    if (op == SQLITE_INDEX_CONSTRAINT_LIMIT) {
      iLimitTerm = i;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_EQ && iColumn == VEC0_COLUMN_ID) {
      if (vtabIn) {
        todo_assert(iRowidInTerm == -1);
        iRowidInTerm = i;

      } else {
        iRowidTerm = i;
      }
    }
    if (op == SQLITE_INDEX_CONSTRAINT_EQ && iColumn == vec0_column_k_idx(p)) {
      iKTerm = i;
    }
  }
  if (iMatchTerm >= 0) {
    if (iLimitTerm < 0 && iKTerm < 0) {
      // TODO: error, match on vector1 should require a limit for KNN. right?
      return SQLITE_ERROR;
    }
    if (iLimitTerm >= 0 && iKTerm >= 0) {
      return SQLITE_ERROR;
    }
    if (pIdxInfo->nOrderBy < 1) {
      // TODO error, `ORDER BY DISTANCE required
      SET_VTAB_ERROR("ORDER BY distance required");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->nOrderBy > 1) {
      // TODO error, orderByConsumed is all or nothing, only 1 order by allowed
      SET_VTAB_ERROR("more than 1 ORDER BY clause provided");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->aOrderBy[0].iColumn != vec0_column_distance_idx(p)) {
      // TODO error, ORDER BY must be on column
      SET_VTAB_ERROR("ORDER BY must be on the distance column");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->aOrderBy[0].desc) {
      // TODO KNN should be ascending, is descending possible?
      SET_VTAB_ERROR("Only ascending in ORDER BY distance clause is supported, "
                     "DESC is not supported yet.");
      return SQLITE_CONSTRAINT;
    }

    pIdxInfo->orderByConsumed = 1;
    pIdxInfo->aConstraintUsage[iMatchTerm].argvIndex = 1;
    pIdxInfo->aConstraintUsage[iMatchTerm].omit = 1;
    if (iLimitTerm >= 0) {
      pIdxInfo->aConstraintUsage[iLimitTerm].argvIndex = 2;
      pIdxInfo->aConstraintUsage[iLimitTerm].omit = 1;
    } else {
      pIdxInfo->aConstraintUsage[iKTerm].argvIndex = 2;
      pIdxInfo->aConstraintUsage[iKTerm].omit = 1;
    }

    sqlite3_str *idxStr = sqlite3_str_new(NULL);
    sqlite3_str_appendall(idxStr, "knn:");
#define VEC0_IDX_KNN_ROWID_IN 'I'
    if (iRowidInTerm >= 0) {
      // already validated as  >= SQLite 3.38 bc iRowidInTerm is only >= 0 when
      // vtabIn == 1
      sqlite3_vtab_in(pIdxInfo, iRowidInTerm, 1);
      sqlite3_str_appendchar(idxStr, VEC0_IDX_KNN_ROWID_IN, 1);
      pIdxInfo->aConstraintUsage[iRowidInTerm].argvIndex = 3;
      pIdxInfo->aConstraintUsage[iRowidInTerm].omit = 1;
    }
    pIdxInfo->idxNum = iMatchVectorTerm;
    pIdxInfo->idxStr = sqlite3_str_finish(idxStr);
    if (!pIdxInfo->idxStr) {
      return SQLITE_NOMEM;
    }
    pIdxInfo->needToFreeIdxStr = 1;
    pIdxInfo->estimatedCost = 30.0;
    pIdxInfo->estimatedRows = 10;

  } else if (iRowidTerm >= 0) {
    pIdxInfo->aConstraintUsage[iRowidTerm].argvIndex = 1;
    pIdxInfo->aConstraintUsage[iRowidTerm].omit = 1;
    pIdxInfo->idxNum = pIdxInfo->colUsed;
    pIdxInfo->idxStr = VEC0_QUERY_PLAN_POINT;
    pIdxInfo->needToFreeIdxStr = 0;
    pIdxInfo->estimatedCost = 10.0;
    pIdxInfo->estimatedRows = 1;
  } else {
    pIdxInfo->idxStr = VEC0_QUERY_PLAN_FULLSCAN;
    pIdxInfo->estimatedCost = 3000000.0;
    pIdxInfo->estimatedRows = 100000;
  }

  return SQLITE_OK;
}

// forward delcaration bc vec0Filter uses it
static int vec0Next(sqlite3_vtab_cursor *cur);

void dethrone(int k, f32 *base_distances, i64 *base_rowids, size_t chunk_size,
              i32 *chunk_top_idx, f32 *chunk_distances, i64 *chunk_rowids,

              i64 **out_rowids, f32 **out_distances) {
  *out_rowids = sqlite3_malloc(k * sizeof(i64));
  todo_assert(out_rowids);
  *out_distances = sqlite3_malloc(k * sizeof(f32));
  todo_assert(out_distances);

  size_t ptrA = 0;
  size_t ptrB = 0;
  for (int i = 0; i < k; i++) {
    if (chunk_distances[chunk_top_idx[ptrA]] < base_distances[ptrB]) {
      (*out_rowids)[i] = chunk_rowids[chunk_top_idx[ptrA]];
      (*out_distances)[i] = chunk_distances[chunk_top_idx[ptrA]];
      // TODO if ptrA at chunk_size-1 is always minimum, won't it always repeat?
      if (ptrA < (chunk_size - 1)) {
        ptrA++;
      }
    } else {
      (*out_rowids)[i] = base_rowids[ptrB];
      (*out_distances)[i] = base_distances[ptrB];
      ptrB++;
    }
  }
}

// TODO: Ya this shit is slow

/**
 * @brief Finds the minimum k items in distances, and writes the indicies to
 * out.
 *
 * @param distances input f32 array of size n, the items to consider.
 * @param n: size of distances array.
 * @param out: Output array of size k, will contain the minumum k element
 * indicies
 * @param k: Size of output array
 * @return int
 */
int min_idx(const f32 *distances, i32 n, i32 *out, i32 k) {
  todo_assert(k > 0);
  todo_assert(k <= n);

  unsigned char *taken = malloc(n * sizeof(unsigned char));
  todo_assert(taken);
  memset(taken, 0, n);

  for (int ik = 0; ik < k; ik++) {
    int min_idx = 0;
    while (min_idx < n && taken[min_idx]) {
      min_idx++;
    }
    todo_assert(min_idx < n);

    for (int i = 0; i < n; i++) {
      if (distances[i] < distances[min_idx] && !taken[i]) {
        min_idx = i;
      }
    }

    out[ik] = min_idx;
    taken[min_idx] = 1;
  }
  free(taken);
  return SQLITE_OK;
}

int vec0Filter_knn(vec0_cursor *pCur, vec0_vtab *p, int idxNum,
                   const char *idxStr, int argc, sqlite3_value **argv) {
  UNUSED_PARAMETER(idxNum);
  UNUSED_PARAMETER(idxStr);
  todo_assert(argc >= 2);
  int rc;
  pCur->query_plan = SQLITE_VEC0_QUERYPLAN_KNN;
  struct vec0_query_knn_data *knn_data =
      sqlite3_malloc(sizeof(struct vec0_query_knn_data));
  if (!knn_data) {
    return SQLITE_NOMEM;
  }
  memset(knn_data, 0, sizeof(struct vec0_query_knn_data));

  int vectorColumnIdx = idxNum;
  struct VectorColumnDefinition *vector_column =
      &p->vector_columns[vectorColumnIdx];

  void *queryVector;
  size_t dimensions;
  enum VectorElementType elementType;
  vector_cleanup cleanup;
  char *err;
  rc = vector_from_value(argv[0], &queryVector, &dimensions, &elementType,
                         &cleanup, &err);
  todo_assert(elementType == vector_column->element_type);
  todo_assert(dimensions == vector_column->dimensions);

  i64 k = sqlite3_value_int64(argv[1]);
  todo_assert(k >= 0);
  if (k == 0) {
    knn_data->k = 0;
    pCur->knn_data = knn_data;
    return SQLITE_OK;
  }

  // handle when a `rowid in (...)` operation was provided
  // Array of all the rowids that appear in any `rowid in (...)` constraint.
  // NULL if none were provided, which means a "full" scan.
  struct Array *arrayRowidsIn = NULL;
  if (argc > 2) {
    sqlite3_value *item;
    int rc;
    arrayRowidsIn = sqlite3_malloc(sizeof(struct Array));
    todo_assert(arrayRowidsIn);
    rc = array_init(arrayRowidsIn, sizeof(i64), 32);
    todo_assert(rc == SQLITE_OK);
    for (rc = sqlite3_vtab_in_first(argv[2], &item); rc == SQLITE_OK && item;
         rc = sqlite3_vtab_in_next(argv[2], &item)) {
      i64 rowid = sqlite3_value_int64(item);
      rc = array_append(arrayRowidsIn, &rowid);
      todo_assert(rc == SQLITE_OK);
    }
    todo_assert(rc == SQLITE_DONE);
    qsort(arrayRowidsIn->z, arrayRowidsIn->length, arrayRowidsIn->element_size,
          _cmp);
  }

  i64 *topk_rowids = sqlite3_malloc(k * sizeof(i64));
  todo_assert(topk_rowids);
  for (int i = 0; i < k; i++) {
    // TODO do we need to ensure that rowid is never -1?
    topk_rowids[i] = -1;
  }
  f32 *topk_distances = sqlite3_malloc(k * sizeof(f32));
  todo_assert(topk_distances);
  for (int i = 0; i < k; i++) {
    topk_distances[i] = FLT_MAX;
  }

  // for each chunk, get top min(k, chunk_size) rowid + distances to query vec.
  // then reconcile all topk_chunks for a true top k.
  // output only rowids + distances for now

  {
    sqlite3_blob *blobVectors;
    sqlite3_stmt *stmtChunks;
    char *zSql;
    zSql = sqlite3_mprintf("select chunk_id, validity, rowids "
                           " from " VEC0_SHADOW_CHUNKS_NAME,
                           p->schemaName, p->tableName);
    todo_assert(zSql);
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtChunks, NULL);
    sqlite3_free(zSql);
    todo_assert(rc == SQLITE_OK);

    void *baseVectors = NULL;
    i64 baseVectorsSize = 0;

    while (true) {
      rc = sqlite3_step(stmtChunks);
      if (rc == SQLITE_DONE)
        break;
      if (rc != SQLITE_ROW) {
        todo("chunks iter error");
      }
      i64 chunk_id = sqlite3_column_int64(stmtChunks, 0);
      unsigned char *chunkValidity =
          (unsigned char *)sqlite3_column_blob(stmtChunks, 1);
      i64 validitySize = sqlite3_column_bytes(stmtChunks, 1);
      todo_assert(validitySize == p->chunk_size / CHAR_BIT);
      i64 *chunkRowids = (i64 *)sqlite3_column_blob(stmtChunks, 2);
      i64 rowidsSize = sqlite3_column_bytes(stmtChunks, 2);
      todo_assert(rowidsSize == p->chunk_size * sizeof(i64));

      // open the vector chunk blob for the current chunk
      rc = sqlite3_blob_open(p->db, p->schemaName,
                             p->shadowVectorChunksNames[vectorColumnIdx],
                             "vectors", chunk_id, 0, &blobVectors);
      todo_assert(rc == SQLITE_OK);
      i64 currentBaseVectorsSize = sqlite3_blob_bytes(blobVectors);
      todo_assert((unsigned long)currentBaseVectorsSize ==
                  p->chunk_size * vector_column_byte_size(*vector_column));

      if (currentBaseVectorsSize > baseVectorsSize) {
        if (baseVectors) {
          sqlite3_free(baseVectors);
        }
        baseVectors = sqlite3_malloc(currentBaseVectorsSize);
        todo_assert(baseVectors);
        baseVectorsSize = currentBaseVectorsSize;
      }
      rc = sqlite3_blob_read(blobVectors, baseVectors, currentBaseVectorsSize,
                             0);
      todo_assert(rc == SQLITE_OK);

      // TODO realloc here, like baseVectors
      f32 *chunk_distances = sqlite3_malloc(p->chunk_size * sizeof(f32));
      todo_assert(chunk_distances);

      for (int i = 0; i < p->chunk_size; i++) {

        // Ensure the current vector is "valid" in the validity bitmap.
        // If not, skip and continue on
        if (!(((chunkValidity[i / CHAR_BIT]) >> (i % CHAR_BIT)) & 1)) {
          chunk_distances[i] = FLT_MAX;
          continue;
        };
        // If pre-filtering, make sure the rowid appears in the `rowid in (...)`
        // list.
        if (arrayRowidsIn) {
          i64 rowid = chunkRowids[i];
          void *in = bsearch(&rowid, arrayRowidsIn->z, arrayRowidsIn->length,
                             sizeof(i64), _cmp);
          if (!in) {
            chunk_distances[i] = FLT_MAX;
            continue;
          }
        }

        f32 result;
        switch (vector_column->element_type) {
        case SQLITE_VEC_ELEMENT_TYPE_FLOAT32: {
          const f32 *base_i =
              ((f32 *)baseVectors) + (i * vector_column->dimensions);
          switch (vector_column->distance_metric) {
          case VEC0_DISTANCE_METRIC_L2: {
            result = distance_l2_sqr_float(base_i, (f32 *)queryVector,
                                           &vector_column->dimensions);
            break;
          }
          case VEC0_DISTANCE_METRIC_COSINE: {
            result = distance_cosine_float(base_i, (f32 *)queryVector,
                                           &vector_column->dimensions);
            break;
          }
          }

          // result = distance_cosine(base_i, (f32 *) queryVector, &
          // vector_column->dimensions);
          break;
        }
        case SQLITE_VEC_ELEMENT_TYPE_INT8: {
          const i8 *base_i =
              ((i8 *)baseVectors) + (i * vector_column->dimensions);
          switch (vector_column->distance_metric) {
          case VEC0_DISTANCE_METRIC_L2: {
            result = distance_l2_sqr_int8(base_i, (i8 *)queryVector,
                                          &vector_column->dimensions);

            break;
          }
          case VEC0_DISTANCE_METRIC_COSINE: {
            result = distance_cosine_int8(base_i, (i8 *)queryVector,
                                          &vector_column->dimensions);
            break;
          }
          }

          break;
        }
        case SQLITE_VEC_ELEMENT_TYPE_BIT: {
          const u8 *base_i = ((u8 *)baseVectors) +
                             (i * (vector_column->dimensions / CHAR_BIT));
          result = distance_hamming(base_i, (u8 *)queryVector,
                                    &vector_column->dimensions);
          break;
        }
        }

        chunk_distances[i] = result;
      }

      // now that we have the distances
      i32 *chunk_topk_idxs = sqlite3_malloc(k * sizeof(i32));
      todo_assert(chunk_topk_idxs);
      min_idx(chunk_distances, p->chunk_size, chunk_topk_idxs,
              k <= p->chunk_size ? k : p->chunk_size);

      i64 *out_rowids;
      f32 *out_distances;
      dethrone(k, topk_distances, topk_rowids, p->chunk_size, chunk_topk_idxs,
               chunk_distances, chunkRowids,

               &out_rowids, &out_distances);
      for (int i = 0; i < k; i++) {
        topk_rowids[i] = out_rowids[i];
        topk_distances[i] = out_distances[i];
      }
      sqlite3_free(out_rowids);
      sqlite3_free(out_distances);
      sqlite3_free(chunk_distances);
      sqlite3_free(chunk_topk_idxs);

      sqlite3_blob_close(blobVectors);
    }

    sqlite3_free(baseVectors);
    rc = sqlite3_finalize(stmtChunks);
    todo_assert(rc == SQLITE_OK);

    if (arrayRowidsIn) {
      array_cleanup(arrayRowidsIn);
      sqlite3_free(arrayRowidsIn);
    }
  }

  cleanup(queryVector);

  knn_data->current_idx = 0;
  knn_data->k = k;
  knn_data->rowids = topk_rowids;
  knn_data->distances = topk_distances;

  pCur->knn_data = knn_data;
  return SQLITE_OK;
}

int vec0Filter_fullscan(vec0_cursor *pCur, vec0_vtab *p, int idxNum,
                        const char *idxStr, int argc, sqlite3_value **argv) {
  UNUSED_PARAMETER(idxNum);
  UNUSED_PARAMETER(idxStr);
  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);
  int rc;
  char *zSql;

  pCur->query_plan = SQLITE_VEC0_QUERYPLAN_FULLSCAN;
  struct vec0_query_fullscan_data *fullscan_data =
      sqlite3_malloc(sizeof(struct vec0_query_fullscan_data));
  if (!fullscan_data) {
    return SQLITE_NOMEM;
  }
  memset(fullscan_data, 0, sizeof(struct vec0_query_fullscan_data));
  zSql = sqlite3_mprintf(" SELECT rowid "
                         " FROM " VEC0_SHADOW_ROWIDS_NAME
                         " ORDER by chunk_id, chunk_offset ",
                         p->schemaName, p->tableName);
  todo_assert(zSql);
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &fullscan_data->rowids_stmt, NULL);
  sqlite3_free(zSql);
  todo_assert(rc == SQLITE_OK);
  rc = sqlite3_step(fullscan_data->rowids_stmt);
  fullscan_data->done = rc == SQLITE_DONE;
  if (!(rc == SQLITE_ROW || rc == SQLITE_DONE)) {
    vec0_query_fullscan_data_clear(fullscan_data);
    return SQLITE_ERROR;
  }
  pCur->fullscan_data = fullscan_data;
  return SQLITE_OK;
}

int vec0Filter_point(vec0_cursor *pCur, vec0_vtab *p, int idxNum,
                     const char *idxStr, int argc, sqlite3_value **argv) {
  UNUSED_PARAMETER(idxNum);
  UNUSED_PARAMETER(idxStr);
  int rc;
  todo_assert(argc == 1);
  i64 rowid = sqlite3_value_int64(argv[0]);

  pCur->query_plan = SQLITE_VEC0_QUERYPLAN_POINT;
  struct vec0_query_point_data *point_data =
      sqlite3_malloc(sizeof(struct vec0_query_point_data));
  if (!point_data) {
    return SQLITE_NOMEM;
  }
  memset(point_data, 0, sizeof(struct vec0_query_point_data));

  for (int i = 0; i < p->numVectorColumns; i++) {
    rc = vec0_get_vector_data(p, rowid, i, &point_data->vectors[i], NULL);
    assert(rc == SQLITE_OK);
  }
  point_data->rowid = rowid;
  point_data->done = 0;
  pCur->point_data = point_data;
  return SQLITE_OK;
}

static int vec0Filter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                      const char *idxStr, int argc, sqlite3_value **argv) {
  vec0_cursor *pCur = (vec0_cursor *)pVtabCursor;
  vec0_vtab *p = (vec0_vtab *)pVtabCursor->pVtab;
  if (strcmp(idxStr, VEC0_QUERY_PLAN_FULLSCAN) == 0) {
    return vec0Filter_fullscan(pCur, p, idxNum, idxStr, argc, argv);
  } else if (strncmp(idxStr, "knn:", 4) == 0) {
    return vec0Filter_knn(pCur, p, idxNum, idxStr, argc, argv);
  } else if (strcmp(idxStr, VEC0_QUERY_PLAN_POINT) == 0) {
    return vec0Filter_point(pCur, p, idxNum, idxStr, argc, argv);
  } else {
    SET_VTAB_CURSOR_ERROR("unknown idxStr");
    return SQLITE_ERROR;
  }
}

static int vec0Rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  UNUSED_PARAMETER(cur);
  UNUSED_PARAMETER(pRowid);
  vec0_cursor *pCur = (vec0_cursor *)cur;
  todo_assert(pCur->query_plan == SQLITE_VEC0_QUERYPLAN_POINT);
  todo_assert(pCur->point_data);
  *pRowid = pCur->point_data->rowid;
  return SQLITE_OK;
}

static int vec0Next(sqlite3_vtab_cursor *cur) {
  vec0_cursor *pCur = (vec0_cursor *)cur;
  switch (pCur->query_plan) {
  case SQLITE_VEC0_QUERYPLAN_FULLSCAN: {
    todo_assert(pCur->fullscan_data);
    int rc = sqlite3_step(pCur->fullscan_data->rowids_stmt);
    if (rc == SQLITE_DONE) {
      pCur->fullscan_data->done = 1;
      return SQLITE_OK;
    }
    if (rc == SQLITE_ROW) {
      // TODO error handle
      return SQLITE_OK;
    }
    return SQLITE_ERROR;
  }
  case SQLITE_VEC0_QUERYPLAN_KNN: {
    todo_assert(pCur->knn_data);
    pCur->knn_data->current_idx++;
    return SQLITE_OK;
  }
  case SQLITE_VEC0_QUERYPLAN_POINT: {
    todo_assert(pCur->point_data);
    pCur->point_data->done = 1;
    return SQLITE_OK;
  }
  default: {
    todo("point next impl");
  }
  }
}

static int vec0Eof(sqlite3_vtab_cursor *cur) {
  vec0_cursor *pCur = (vec0_cursor *)cur;
  switch (pCur->query_plan) {
  case SQLITE_VEC0_QUERYPLAN_FULLSCAN: {
    todo_assert(pCur->fullscan_data);
    return pCur->fullscan_data->done;
  }
  case SQLITE_VEC0_QUERYPLAN_KNN: {
    todo_assert(pCur->knn_data);
    return (pCur->knn_data->current_idx >= pCur->knn_data->k) ||
           (pCur->knn_data->distances[pCur->knn_data->current_idx] ==
            FLT_MAX);
  }
  case SQLITE_VEC0_QUERYPLAN_POINT: {
    todo_assert(pCur->point_data);
    return pCur->point_data->done;
  }
  }
}

static int vec0Column_fullscan(vec0_vtab *pVtab, vec0_cursor *pCur,
                               sqlite3_context *context, int i) {
  todo_assert(pCur->fullscan_data);
  i64 rowid = sqlite3_column_int64(pCur->fullscan_data->rowids_stmt, 0);
  if (i == VEC0_COLUMN_ID) {
    vec0_result_id(pVtab, context, rowid);
  } else if (vec0_column_idx_is_vector(pVtab, i)) {
    void *v;
    int sz;
    int vector_idx = vec0_column_idx_to_vector_idx(pVtab, i);
    int rc = vec0_get_vector_data(pVtab, rowid, vector_idx, &v, &sz);
    todo_assert(rc == SQLITE_OK);
    sqlite3_result_blob(context, v, sz, SQLITE_TRANSIENT);
    sqlite3_result_subtype(context,
                           pVtab->vector_columns[vector_idx].element_type);

    sqlite3_free(v);
  } else if (i == vec0_column_distance_idx(pVtab)) {
    sqlite3_result_null(context);
  } else {
    sqlite3_result_null(context);
  }
  return SQLITE_OK;
}

static int vec0Column_point(vec0_vtab *pVtab, vec0_cursor *pCur,
                            sqlite3_context *context, int i) {
  todo_assert(pCur->point_data);
  if (i == VEC0_COLUMN_ID) {
    vec0_result_id(pVtab, context, pCur->point_data->rowid);
    return SQLITE_OK;
  }
  if (i == vec0_column_distance_idx(pVtab)) {
    sqlite3_result_null(context);
    return SQLITE_OK;
  }
  // TODO only have 1st vector data
  if (vec0_column_idx_is_vector(pVtab, i)) {
    int vector_idx = vec0_column_idx_to_vector_idx(pVtab, i);
    sqlite3_result_blob(
        context, pCur->point_data->vectors[vector_idx],
        vector_column_byte_size(pVtab->vector_columns[vector_idx]),
        SQLITE_TRANSIENT);
    sqlite3_result_subtype(context,
                           pVtab->vector_columns[vector_idx].element_type);
    return SQLITE_OK;
  }

  return SQLITE_OK;
}

static int vec0Column_knn(vec0_vtab *pVtab, vec0_cursor *pCur,
                          sqlite3_context *context, int i) {
  todo_assert(pCur->knn_data);
  if (i == VEC0_COLUMN_ID) {
    i64 rowid = pCur->knn_data->rowids[pCur->knn_data->current_idx];
    vec0_result_id(pVtab, context, rowid);
    return SQLITE_OK;
  }
  if (i == vec0_column_distance_idx(pVtab)) {
    sqlite3_result_double(
        context, pCur->knn_data->distances[pCur->knn_data->current_idx]);
    return SQLITE_OK;
  }
  if (vec0_column_idx_is_vector(pVtab, i)) {
    void *out;
    int sz;
    int rc = vec0_get_vector_data(
        pVtab, pCur->knn_data->rowids[pCur->knn_data->current_idx],
        vec0_column_idx_to_vector_idx(pVtab, i), &out, &sz);
    todo_assert(rc == SQLITE_OK);
    sqlite3_result_blob(context, out, sz, sqlite3_free);
    return SQLITE_OK;
  }

  return SQLITE_OK;
}

static int vec0Column(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                      int i) {
  vec0_cursor *pCur = (vec0_cursor *)cur;
  vec0_vtab *pVtab = (vec0_vtab *)cur->pVtab;
  switch (pCur->query_plan) {
  case SQLITE_VEC0_QUERYPLAN_FULLSCAN: {
    return vec0Column_fullscan(pVtab, pCur, context, i);
  }
  case SQLITE_VEC0_QUERYPLAN_KNN: {
    return vec0Column_knn(pVtab, pCur, context, i);
  }
  case SQLITE_VEC0_QUERYPLAN_POINT: {
    return vec0Column_point(pVtab, pCur, context, i);
  }
  }
  return SQLITE_OK;
}

/**
 * @brief Handles the "insert rowid" step of a row insert operation of a vec0
 * table.
 *
 * This function will insert a new row into the _rowids vec0 shadow table.
 *
 * @param p: virtual table
 * @param idValue: Value containing the inserted rowid/id value.
 * @param rowid: Output rowid, will point to the "real" i64 rowid
 * value that was inserted
 * @return int SQLITE_OK on success, error code on failure
 */
int vec0Update_InsertRowidStep(vec0_vtab *p, sqlite3_value *idValue,
                               i64 *rowid) {

  /**
   * An insert into a vec0 table can happen a few different ways:
   *  1) With default INTEGER primary key: With a supplied i64 rowid
   *  2) With default INTEGER primary key: WITHOUT a supplied rowid
   *  3) With TEXT primary key: supplied text rowid
   */

  int rc;

  // Option 3: vtab has a user-defined TEXT primary key, so ensure a text value
  // is provided.
  if (p->pkIsText) {
    todo_assert(sqlite3_value_type(idValue) == SQLITE_TEXT);

#ifdef SQLITE_VEC_THREADSAFE
    sqlite3_mutex_enter(sqlite3_db_mutex(p->db));
#endif
    sqlite3_reset(p->stmtRowidsInsertId);
    sqlite3_clear_bindings(p->stmtRowidsInsertId);
    sqlite3_bind_value(p->stmtRowidsInsertId, 1, idValue);
    rc = sqlite3_step(p->stmtRowidsInsertId);
    todo_assert(rc == SQLITE_DONE);
    *rowid = sqlite3_last_insert_rowid(p->db);
#ifdef SQLITE_VEC_THREADSAFE
    sqlite3_mutex_leave(sqlite3_db_mutex(p->db));
#endif

  }
  // Option 1: User supplied a i64 rowid
  else if (sqlite3_value_type(idValue) == SQLITE_INTEGER) {
    i64 suppliedRowid = sqlite3_value_int64(idValue);

    sqlite3_reset(p->stmtRowidsInsertRowid);
    sqlite3_clear_bindings(p->stmtRowidsInsertRowid);
    sqlite3_bind_int64(p->stmtRowidsInsertRowid, 1, suppliedRowid);
    rc = sqlite3_step(p->stmtRowidsInsertRowid);
    todo_assert(rc == SQLITE_DONE);
    *rowid = suppliedRowid;
  }
  // Option 2: User did not suppled a rowid
  else {
    todo_assert(sqlite3_value_type(idValue) == SQLITE_NULL);
#ifdef SQLITE_VEC_THREADSAFE
    sqlite3_mutex_enter(sqlite3_db_mutex(p->db));
#endif
    sqlite3_reset(p->stmtRowidsInsertId);
    sqlite3_clear_bindings(p->stmtRowidsInsertId);
    // no need to bind a value to ?1 here: needs to be NULL
    // so we can get the next autoincremented rowid value.
    rc = sqlite3_step(p->stmtRowidsInsertId);
    todo_assert(rc == SQLITE_DONE);
    *rowid = sqlite3_last_insert_rowid(p->db);
#ifdef SQLITE_VEC_THREADSAFE
    sqlite3_mutex_leave(sqlite3_db_mutex(p->db));
#endif
  }
  return SQLITE_OK;
}

/**
 * @brief Determines the "next available" chunk position for a newly inserted
 * vec0 row.
 *
 * This operation may insert a new "blank" chunk the _chunks table, if there is
 * no more space in previous chunks.
 *
 * @param p: virtual table
 * @param chunk_rowid: Output rowid of the chunk in the _chunks virtual table
 * that has the avialabiity.
 * @param chunk_offset: Output the index of the available space insert the
 * chunk, based on the index of the first available validity bit.
 * @param pBlobValidity: Output blob of the validity column of the available
 * chunk. Will be opened with read/write permissions.
 * @param pValidity: Output buffer of the original chunk's validity column.
 *    Needs to be cleaned up with sqlite3_free().
 * @return int SQLITE_OK on success, error code on failure
 */
int vec0Update_InsertNextAvailableStep(
    vec0_vtab *p, i64 *chunk_rowid, i64 *chunk_offset,
    sqlite3_blob **blobChunksValidity,
    const unsigned char **bufferChunksValidity) {

  int rc;
  i64 validitySize;
  *chunk_offset = -1;

  sqlite3_reset(p->stmtLatestChunk);
  rc = sqlite3_step(p->stmtLatestChunk);
  todo_assert(rc == SQLITE_ROW);
  *chunk_rowid = sqlite3_column_int64(p->stmtLatestChunk, 0);
  rc = sqlite3_step(p->stmtLatestChunk);
  todo_assert(rc == SQLITE_DONE);

  rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowChunksName, "validity",
                         *chunk_rowid, 1, blobChunksValidity);
  todo_assert(rc == SQLITE_OK);

  validitySize = sqlite3_blob_bytes(*blobChunksValidity);
  todo_assert(validitySize == p->chunk_size / CHAR_BIT);

  *bufferChunksValidity = sqlite3_malloc(validitySize);
  todo_assert(*bufferChunksValidity);

  rc = sqlite3_blob_read(*blobChunksValidity, (void *)*bufferChunksValidity,
                         validitySize, 0);
  todo_assert(rc == SQLITE_OK);

  for (int i = 0; i < validitySize; i++) {
    if ((*bufferChunksValidity)[i] == 0b11111111)
      continue;
    for (int j = 0; j < CHAR_BIT; j++) {
      if (((((*bufferChunksValidity)[i] >> j) & 1) == 0)) {
        *chunk_offset = (i * CHAR_BIT) + j;
        goto done;
      }
    }
  }

done:
  // latest chunk was full, so need to create a new one
  if (*chunk_offset == -1) {
    int rc = vec0_new_chunk(p, chunk_rowid);
    assert(rc == SQLITE_OK);
    *chunk_offset = 0;

    // blobChunksValidity and pValidity are stale, pointing to the previous
    // (full) chunk. to re-assign them
    sqlite3_blob_close(*blobChunksValidity);
    sqlite3_free((void *)*bufferChunksValidity);

    rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowChunksName,
                           "validity", *chunk_rowid, 1, blobChunksValidity);
    todo_assert(rc == SQLITE_OK);
    validitySize = sqlite3_blob_bytes(*blobChunksValidity);
    todo_assert(validitySize == p->chunk_size / CHAR_BIT);
    *bufferChunksValidity = sqlite3_malloc(validitySize);
    rc = sqlite3_blob_read(*blobChunksValidity, (void *)*bufferChunksValidity,
                           validitySize, 0);
    todo_assert(rc == SQLITE_OK);
  }

  return SQLITE_OK;
}

static int vec0Update_InsertWriteFinalStepVectors(
    sqlite3_blob *blobVectors, const void *bVector, i64 chunk_offset,
    size_t dimensions, enum VectorElementType element_type) {
  int n;
  int offset;

  switch (element_type) {
  case SQLITE_VEC_ELEMENT_TYPE_FLOAT32:
    n = dimensions * sizeof(f32);
    offset = chunk_offset * dimensions * sizeof(f32);
    break;
  case SQLITE_VEC_ELEMENT_TYPE_INT8:
    n = dimensions * sizeof(i8);
    offset = chunk_offset * dimensions * sizeof(i8);
    break;
  case SQLITE_VEC_ELEMENT_TYPE_BIT:
    n = dimensions / CHAR_BIT;
    offset = chunk_offset * dimensions / CHAR_BIT;
    break;
  }

  int rc = sqlite3_blob_write(blobVectors, bVector, n, offset);
  todo_assert(rc == SQLITE_OK);
  return rc;
}

/**
 * @brief
 *
 * @param p vec0 virtual table
 * @param chunk_rowid: which chunk to write to
 * @param chunk_offset: the offset inside the chunk to write the vector to.
 * @param rowid: the rowid of the inserting row
 * @param vectorDatas: array of the vector data to insert
 * @param blobValidity: writeable validity blob of the row's assigned chunk.
 * @param validity: snapshot buffer of the valdity column from the row's
 * assigned chunk.
 * @return int SQLITE_OK on success, error code on failure
 */
int vec0Update_InsertWriteFinalStep(vec0_vtab *p, i64 chunk_rowid,
                                    i64 chunk_offset, i64 rowid,
                                    void *vectorDatas[],
                                    sqlite3_blob *blobChunksValidity,
                                    const unsigned char *bufferChunksValidity) {
  int rc;
  sqlite3_blob *blobChunksRowids;

  // mark the validity bit for this row in the chunk's validity bitmap
  // Get the byte offset of the bitmap
  char unsigned bx = bufferChunksValidity[chunk_offset / CHAR_BIT];
  // set the bit at the chunk_offset position inside that byte
  bx = bx | (1 << (chunk_offset % CHAR_BIT));
  // write that 1 byte
  rc = sqlite3_blob_write(blobChunksValidity, &bx, 1, chunk_offset / CHAR_BIT);
  todo_assert(rc == SQLITE_OK);

  // Go insert the vector data into the vector chunk shadow tables
  for (int i = 0; i < p->numVectorColumns; i++) {
    sqlite3_blob *blobVectors;
    rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowVectorChunksNames[i],
                           "vectors", chunk_rowid, 1, &blobVectors);
    todo_assert(rc == SQLITE_OK);

    switch (p->vector_columns[i].element_type) {
    case SQLITE_VEC_ELEMENT_TYPE_FLOAT32:
      todo_assert((unsigned long)sqlite3_blob_bytes(blobVectors) ==
                  p->chunk_size * p->vector_columns[i].dimensions *
                      sizeof(f32));
      break;
    case SQLITE_VEC_ELEMENT_TYPE_INT8:
      todo_assert((unsigned long)sqlite3_blob_bytes(blobVectors) ==
                  p->chunk_size * p->vector_columns[i].dimensions * sizeof(i8));
      break;
    case SQLITE_VEC_ELEMENT_TYPE_BIT:
      todo_assert((unsigned long)sqlite3_blob_bytes(blobVectors) ==
                  p->chunk_size * p->vector_columns[i].dimensions / CHAR_BIT);
      break;
    }

    rc = vec0Update_InsertWriteFinalStepVectors(
        blobVectors, vectorDatas[i], chunk_offset,
        p->vector_columns[i].dimensions, p->vector_columns[i].element_type);
    todo_assert(rc == SQLITE_OK);
    sqlite3_blob_close(blobVectors);
  }

  // write the new rowid to the rowids column of the _chunks table
  rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowChunksName, "rowids",
                         chunk_rowid, 1, &blobChunksRowids);
  todo_assert(rc == SQLITE_OK);
  todo_assert(sqlite3_blob_bytes(blobChunksRowids) ==
              p->chunk_size * sizeof(i64));
  rc = sqlite3_blob_write(blobChunksRowids, &rowid, sizeof(i64),
                          chunk_offset * sizeof(i64));
  todo_assert(rc == SQLITE_OK);
  sqlite3_blob_close(blobChunksRowids);

  // Now with all the vectors inserted, go back and update the _rowids table
  // with the new chunk_rowid/chunk_offset values
  sqlite3_reset(p->stmtRowidsUpdatePosition);
  sqlite3_clear_bindings(p->stmtRowidsUpdatePosition);
  sqlite3_bind_int64(p->stmtRowidsUpdatePosition, 1, chunk_rowid);
  sqlite3_bind_int64(p->stmtRowidsUpdatePosition, 2, chunk_offset);
  sqlite3_bind_int64(p->stmtRowidsUpdatePosition, 3, rowid);
  rc = sqlite3_step(p->stmtRowidsUpdatePosition);
  todo_assert(rc == SQLITE_DONE);

  return SQLITE_OK;
}

/**
 * @brief Handles INSERT INTO operations on a vec0 table.
 *
 * @return int SQLITE_OK on success, otherwise error code on failure
 */
int vec0Update_Insert(sqlite3_vtab *pVTab, int argc, sqlite3_value **argv,
                      sqlite_int64 *pRowid) {
  UNUSED_PARAMETER(argc);
  vec0_vtab *p = (vec0_vtab *)pVTab;
  int rc;
  // Rowid for the inserted row, deterimined by the inserted ID + _rowids shadow
  // table
  i64 rowid;
  // Array to hold the vector data of the inserted row. Individual elements will
  // have a lifetime bound to the argv[..] values.
  void *vectorDatas[VEC0_MAX_VECTOR_COLUMNS];

  // Rowid of the chunk in the _chunks shadow table that the row will be a part
  // of.
  i64 chunk_rowid;
  // offset within the chunk where the rowid belongs
  i64 chunk_offset;

  // a write-able blob of the validity column for the given chunk. Used to mark
  // validity bit
  sqlite3_blob *blobChunksValidity;
  // buffer for the valididty column for the given chunk. TODO maybe not needed
  // here?
  const unsigned char *bufferChunksValidity;

  vector_cleanup cleanups[VEC0_MAX_VECTOR_COLUMNS];
  // read all the inserted vectors  into vectorDatas, validate their lengths.
  for (int i = 0; i < p->numVectorColumns; i++) {
    sqlite3_value *valueVector = argv[2 + VEC0_COLUMN_VECTORN_START + i];
    size_t dimensions;

    char *pzError;
    enum VectorElementType elementType;
    int rc = vector_from_value(valueVector, &vectorDatas[i], &dimensions,
                               &elementType, &cleanups[i], &pzError);
    todo_assert(rc == SQLITE_OK);
    assert(elementType == p->vector_columns[i].element_type);

    if (dimensions != p->vector_columns[i].dimensions) {
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg = sqlite3_mprintf(
          "Dimension mismatch for inserted vector for the \"%.*s\" column. "
          "Expected %d dimensions but received %d.",
          p->vector_columns[i].name_length, p->vector_columns[i].name,
          p->vector_columns[i].dimensions, dimensions);
      return SQLITE_ERROR;
    }
  }

  // Cannot insert a value in the hidden "distance" column
  if (sqlite3_value_type(argv[2 + vec0_column_distance_idx(p)]) !=
      SQLITE_NULL) {
    SET_VTAB_ERROR("TODO distance provided in INSERT operation.");
    return SQLITE_ERROR;
  }
  // Cannot insert a value in the hidden "k" column
  if (sqlite3_value_type(argv[2 + vec0_column_k_idx(p)]) != SQLITE_NULL) {
    SET_VTAB_ERROR("TODO k provided in INSERT operation.");
    return SQLITE_ERROR;
  }

  // Step #1: Insert/get a rowid for this row, from the _rowids table.
  rc = vec0Update_InsertRowidStep(p, argv[2 + VEC0_COLUMN_ID], &rowid);
  todo_assert(rc == SQLITE_OK);

  // Step #2: Find the next "available" position in the _chunks table for this
  // row.
  rc = vec0Update_InsertNextAvailableStep(p, &chunk_rowid, &chunk_offset,
                                          &blobChunksValidity,
                                          &bufferChunksValidity);
  todo_assert(rc == SQLITE_OK);

  // Step #3: With the next available chunk position, write out all the vectors
  //          to their specified location.
  rc = vec0Update_InsertWriteFinalStep(p, chunk_rowid, chunk_offset, rowid,
                                       vectorDatas, blobChunksValidity,
                                       bufferChunksValidity);
  todo_assert(rc == SQLITE_OK);

  for (int i = 0; i < p->numVectorColumns; i++) {
    cleanups[i](vectorDatas[i]);
  }

  sqlite3_blob_close(blobChunksValidity);
  sqlite3_free((void *)bufferChunksValidity);
  *pRowid = rowid;

  return SQLITE_OK;
}

int vec0Update_Delete(sqlite3_vtab *pVTab, sqlite_int64 rowid) {
  vec0_vtab *p = (vec0_vtab *)pVTab;
  int rc;
  i64 chunk_id;
  i64 chunk_offset;
  sqlite3_blob *blobChunksValidity = NULL;

  // 1. get chunk_id and chunk_offset from _rowids
  rc = vec0_get_chunk_position(p, rowid, &chunk_id, &chunk_offset);
  todo_assert(rc == SQLITE_OK);

  // 2. ensure chunks.validity bit is 1, then set to 0
  rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowChunksName, "validity",
                         chunk_id, 1, &blobChunksValidity);
  assert(rc == SQLITE_OK);
  char unsigned bx;
  rc = sqlite3_blob_read(blobChunksValidity, &bx, sizeof(bx),
                         chunk_offset / CHAR_BIT);
  todo_assert(rc == SQLITE_OK);
  todo_assert(bx >> (chunk_offset % CHAR_BIT));
  char unsigned mask = ~(1 << (chunk_offset % CHAR_BIT));
  char result = bx & mask;
  rc = sqlite3_blob_write(blobChunksValidity, &result, sizeof(bx),
                          chunk_offset / CHAR_BIT);
  todo_assert(rc == SQLITE_OK);
  sqlite3_blob_close(blobChunksValidity);

  // 3. zero out rowid in chunks.rowids TODO

  // 4. zero out any data in vector chunks tables TODO

  // 5. delete from _rowids table
  char *zSql =
      sqlite3_mprintf("DELETE FROM " VEC0_SHADOW_ROWIDS_NAME " WHERE rowid = ?",
                      p->schemaName, p->tableName);
  todo_assert(zSql);
  sqlite3_stmt *stmt;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  todo_assert(rc == SQLITE_OK);
  sqlite3_bind_int64(stmt, 1, rowid);
  rc = sqlite3_step(stmt);
  todo_assert(SQLITE_DONE);
  sqlite3_finalize(stmt);

  return SQLITE_OK;
}

int vec0Update_UpdateOnRowid(sqlite3_vtab *pVTab, int argc,
                             sqlite3_value **argv) {
  UNUSED_PARAMETER(argc);
  vec0_vtab *p = (vec0_vtab *)pVTab;
  int rc;
  i64 chunk_id;
  i64 chunk_offset;
  i64 rowid = sqlite3_value_int64(argv[0]);

  // 1. get chunk_id and chunk_offset from _rowids
  rc = vec0_get_chunk_position(p, rowid, &chunk_id, &chunk_offset);
  todo_assert(rc == SQLITE_OK);

  // 2) iterate over all new vectors, update the vectors

  // read all the inserted vectors  into vectorDatas, validate their lengths.
  for (int i = 0; i < p->numVectorColumns; i++) {
    sqlite3_value *valueVector = argv[2 + VEC0_COLUMN_VECTORN_START + i];
    size_t dimensions;
    void *vector = (void *)sqlite3_value_blob(valueVector);
    switch (p->vector_columns[i].element_type) {
    case SQLITE_VEC_ELEMENT_TYPE_FLOAT32:
      dimensions = sqlite3_value_bytes(valueVector) / sizeof(f32);
      break;
    case SQLITE_VEC_ELEMENT_TYPE_INT8:
      dimensions = sqlite3_value_bytes(valueVector) * sizeof(i8);
      break;
    case SQLITE_VEC_ELEMENT_TYPE_BIT:
      dimensions = sqlite3_value_bytes(valueVector) * CHAR_BIT;
      break;
    }
    if (dimensions != p->vector_columns[i].dimensions) {
      SET_VTAB_ERROR("TODO vector length dont make sense.");
      sqlite3_free(pVTab->zErrMsg);
      pVTab->zErrMsg =
          sqlite3_mprintf("Vector length mismatch on '%s' column: Expected %d "
                          "dimensions, found %d",
                          p->vector_columns[i].name,
                          p->vector_columns[i].dimensions, dimensions);
      return SQLITE_ERROR;
    }

    sqlite3_blob *blobVectors;
    rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowVectorChunksNames[i],
                           "vectors", chunk_id, 1, &blobVectors);
    todo_assert(rc == SQLITE_OK);
    // TODO rename this functions
    rc = vec0Update_InsertWriteFinalStepVectors(
        blobVectors, vector, chunk_offset, p->vector_columns[i].dimensions,
        p->vector_columns[i].element_type);
    todo_assert(rc == SQLITE_OK);
    sqlite3_blob_close(blobVectors);
  }

  return SQLITE_OK;
}

static int vec0Update(sqlite3_vtab *pVTab, int argc, sqlite3_value **argv,
                      sqlite_int64 *pRowid) {
  // DELETE operation
  if (argc == 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL) {
    return vec0Update_Delete(pVTab, sqlite3_value_int64(argv[0]));
  }
  // INSERT operation
  else if (argc > 1 && sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    return vec0Update_Insert(pVTab, argc, argv, pRowid);
  }
  // UPDATE operation
  else if (argc > 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL) {
    if ((sqlite3_value_type(argv[0]) == SQLITE_INTEGER) &&
        (sqlite3_value_type(argv[1]) == SQLITE_INTEGER) &&
        (sqlite3_value_int64(argv[0]) == sqlite3_value_int64(argv[1]))) {
      return vec0Update_UpdateOnRowid(pVTab, argc, argv);
    }

    SET_VTAB_ERROR("UPDATE operation on rowids with vec0 is not supported.");
    return SQLITE_ERROR;
  }
  // unknown operation
  else {
    SET_VTAB_ERROR("Unrecognized xUpdate operation provided for vec0.");
    return SQLITE_ERROR;
  }
}

static int vec0ShadowName(const char *zName) {
  // TODO multiple vector_chunk tables
  static const char *azName[] = {"rowids", "chunks", "vector_chunks"};

  for (size_t i = 0; i < sizeof(azName) / sizeof(azName[0]); i++) {
    if (sqlite3_stricmp(zName, azName[i]) == 0)
      return 1;
  }
  return 0;
}

static sqlite3_module vec0Module = {
    /* iVersion      */ 3,
    /* xCreate       */ vec0Create,
    /* xConnect      */ vec0Connect,
    /* xBestIndex    */ vec0BestIndex,
    /* xDisconnect   */ vec0Disconnect,
    /* xDestroy      */ vec0Destroy,
    /* xOpen         */ vec0Open,
    /* xClose        */ vec0Close,
    /* xFilter       */ vec0Filter,
    /* xNext         */ vec0Next,
    /* xEof          */ vec0Eof,
    /* xColumn       */ vec0Column,
    /* xRowid        */ vec0Rowid,
    /* xUpdate       */ vec0Update,
    /* xBegin        */ 0,
    /* xSync         */ 0,
    /* xCommit       */ 0,
    /* xRollback     */ 0,
    /* xFindFunction */ 0,
    /* xRename       */ 0, // TODO
    /* xSavepoint    */ 0,
    /* xRelease      */ 0,
    /* xRollbackTo   */ 0,
    /* xShadowName   */ vec0ShadowName,
    #if SQLITE_VERSION_NUMBER >= 3440000
    /* xIntegrity    */ 0, // TODO
    #endif
};
#pragma endregion

#ifdef SQLITE_VEC_ENABLE_EXPERIMENTAL
static char * POINTER_NAME_STATIC_BLOB_DEF = "vec0-static_blob_def";
struct static_blob_definition {
  void * p;
  size_t dimensions;
  size_t nvectors;
  enum VectorElementType element_type;
};
static void vec_static_blob_from_raw(sqlite3_context *context, int argc, sqlite3_value **argv) {
  struct static_blob_definition * p;
  p = sqlite3_malloc(sizeof(*p));
  todo_assert(p);
  p->p = sqlite3_value_int64(argv[0]);
  p->element_type = SQLITE_VEC_ELEMENT_TYPE_FLOAT32;
  p->dimensions = sqlite3_value_int64(argv[2]);
  p->nvectors = sqlite3_value_int64(argv[3]);
  sqlite3_result_pointer(context, p, POINTER_NAME_STATIC_BLOB_DEF, sqlite3_free);
}
#pragma region vec_static_blobs() table function

#define MAX_STATIC_BLOBS 16

typedef struct static_blob static_blob;
struct static_blob {
  char * name;
  void * p;
  size_t dimensions;
  size_t nvectors;
  enum VectorElementType element_type;
};

typedef struct vec_static_blob_data vec_static_blob_data;
struct vec_static_blob_data {
  static_blob static_blobs[MAX_STATIC_BLOBS];
};

typedef struct vec_static_blobs_vtab vec_static_blobs_vtab;
struct vec_static_blobs_vtab {
  sqlite3_vtab base;
  vec_static_blob_data * data;
};

typedef struct vec_static_blobs_cursor vec_static_blobs_cursor;
struct vec_static_blobs_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
};

static int vec_static_blobsConnect(sqlite3 *db, void *pAux, int argc,
                          const char *const *argv, sqlite3_vtab **ppVtab,
                          char **pzErr) {
  vec_static_blobs_vtab *pNew;
#define VEC_STATIC_BLOBS_NAME         0
#define VEC_STATIC_BLOBS_DATA         1
#define VEC_STATIC_BLOBS_DIMENSIONS   2
#define VEC_STATIC_BLOBS_COUNT        3
  int rc = sqlite3_declare_vtab(
      db, "CREATE TABLE x(name, data, dimensions hidden, count hidden)");
  if (rc == SQLITE_OK) {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
      return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    pNew->data = pAux;
  }
  return rc;
}

static int vec_static_blobsDisconnect(sqlite3_vtab *pVtab) {
  vec_static_blobs_vtab *p = (vec_static_blobs_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int vec_static_blobsUpdate(sqlite3_vtab *pVTab, int argc, sqlite3_value **argv,
                      sqlite_int64 *pRowid) {
  vec_static_blobs_vtab* p  = (vec_static_blobs_vtab*) pVTab;
  // DELETE operation
  if (argc == 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL) {
    return SQLITE_ERROR;
  }
  // INSERT operation
  else if (argc > 1 && sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    const char * key = sqlite3_value_text(argv[2 + VEC_STATIC_BLOBS_NAME]);
    int idx = -1;
    for(int i = 0; i < MAX_STATIC_BLOBS; i++) {
      if(!p->data->static_blobs[i].name) {
        p->data->static_blobs[i].name = sqlite3_mprintf("%s", key);
        idx = i;
        break;
      }
    }
    if(idx < 0) abort();
    struct static_blob_definition * def = sqlite3_value_pointer(argv[2 + VEC_STATIC_BLOBS_DATA], POINTER_NAME_STATIC_BLOB_DEF);
    p->data->static_blobs[idx].p = def->p;
    p->data->static_blobs[idx].dimensions = def->dimensions;
    p->data->static_blobs[idx].nvectors = def->nvectors;
    p->data->static_blobs[idx].element_type = def->element_type;

    return SQLITE_OK;
  }
  // UPDATE operation
  else if (argc > 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL) {
    return SQLITE_ERROR;
  }
  return SQLITE_ERROR;
}

static int vec_static_blobsOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  vec_static_blobs_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int vec_static_blobsClose(sqlite3_vtab_cursor *cur) {
  vec_static_blobs_cursor *pCur = (vec_static_blobs_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int vec_static_blobsBestIndex(sqlite3_vtab *pVTab, sqlite3_index_info *pIdxInfo) {
  pIdxInfo->idxNum = 1;
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

static int vec_static_blobsNext(sqlite3_vtab_cursor *cur);
static int vec_static_blobsFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                         const char *idxStr, int argc, sqlite3_value **argv) {
  vec_static_blobs_cursor *pCur = (vec_static_blobs_cursor *)pVtabCursor;
  pCur->iRowid = -1;
  vec_static_blobsNext(pVtabCursor);
  return SQLITE_OK;
}

static int vec_static_blobsRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  vec_static_blobs_cursor *pCur = (vec_static_blobs_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int vec_static_blobsNext(sqlite3_vtab_cursor *cur) {
  vec_static_blobs_cursor *pCur = (vec_static_blobs_cursor *)cur;
  vec_static_blobs_vtab * p = (vec_static_blobs_vtab *) pCur->base.pVtab;
  pCur->iRowid++;
  while(pCur->iRowid < MAX_STATIC_BLOBS) {
    if(p->data->static_blobs[pCur->iRowid].name) {
      return SQLITE_OK;
    }
    pCur->iRowid++;
  }
  return SQLITE_OK;
}

static int vec_static_blobsEof(sqlite3_vtab_cursor *cur) {
  vec_static_blobs_cursor *pCur = (vec_static_blobs_cursor *)cur;
  return pCur->iRowid >= MAX_STATIC_BLOBS;
}

static int vec_static_blobsColumn(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                         int i) {
  vec_static_blobs_cursor *pCur = (vec_static_blobs_cursor *)cur;
  vec_static_blobs_vtab *p = (vec_static_blobs_vtab *)cur->pVtab;
  switch (i) {
  case VEC_STATIC_BLOBS_NAME:
    sqlite3_result_text(context, p->data->static_blobs[pCur->iRowid].name, -1, SQLITE_TRANSIENT);
    break;
  case VEC_STATIC_BLOBS_DATA:
    sqlite3_result_null(context);
    break;
  case VEC_STATIC_BLOBS_DIMENSIONS:
    sqlite3_result_int64(context,  p->data->static_blobs[pCur->iRowid].dimensions);
    break;
  case VEC_STATIC_BLOBS_COUNT:
    sqlite3_result_int64(context,  p->data->static_blobs[pCur->iRowid].nvectors);
    break;
  }
  return SQLITE_OK;
}


static sqlite3_module vec_static_blobsModule = {
    /* iVersion    */ 3,
    /* xCreate     */ 0,
    /* xConnect    */ vec_static_blobsConnect,
    /* xBestIndex  */ vec_static_blobsBestIndex,
    /* xDisconnect */ vec_static_blobsDisconnect,
    /* xDestroy    */ 0,
    /* xOpen       */ vec_static_blobsOpen,
    /* xClose      */ vec_static_blobsClose,
    /* xFilter     */ vec_static_blobsFilter,
    /* xNext       */ vec_static_blobsNext,
    /* xEof        */ vec_static_blobsEof,
    /* xColumn     */ vec_static_blobsColumn,
    /* xRowid      */ vec_static_blobsRowid,
    /* xUpdate     */ vec_static_blobsUpdate,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0};
#pragma endregion


#pragma region vec_static_blob_entries() table function

typedef struct vec_static_blob_entries_vtab vec_static_blob_entries_vtab;
struct vec_static_blob_entries_vtab {
  sqlite3_vtab base;
  static_blob * blob;
};
typedef enum {
  VEC_SBE__QUERYPLAN_FULLSCAN = 1,
  VEC_SBE__QUERYPLAN_KNN = 2
} vec_sbe_query_plan;

typedef struct vec_static_blob_entries_cursor vec_static_blob_entries_cursor;
struct vec_static_blob_entries_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
  vec_sbe_query_plan query_plan;
  struct vec0_query_knn_data * knn_data;
};


static int vec_static_blob_entriesConnect(sqlite3 *db, void *pAux, int argc,
                          const char *const *argv, sqlite3_vtab **ppVtab,
                          char **pzErr) {
  vec_static_blob_data * blob_data = pAux;
  int idx = -1;
  for(int i = 0; i < MAX_STATIC_BLOBS; i++) {
    if(!blob_data->static_blobs[i].name) continue;
    if(strncmp(blob_data->static_blobs[i].name, argv[3], strlen(blob_data->static_blobs[i].name))==0) {
      idx = i;
      break;
    }
  }
  if(idx < 0) abort();
  vec_static_blob_entries_vtab *pNew;
#define VEC_STATIC_BLOB_ENTRIES_VECTOR   0
#define VEC_STATIC_BLOB_ENTRIES_DISTANCE 1
#define VEC_STATIC_BLOB_ENTRIES_K        2
  int rc = sqlite3_declare_vtab(
      db, "CREATE TABLE x(vector, distance hidden, k hidden)");
  if (rc == SQLITE_OK) {
    pNew = sqlite3_malloc(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab *)pNew;
    if (pNew == 0)
      return SQLITE_NOMEM;
    memset(pNew, 0, sizeof(*pNew));
    pNew->blob = &blob_data->static_blobs[idx];
  }
  return rc;
}

static int vec_static_blob_entriesCreate(sqlite3 *db, void *pAux, int argc,
                          const char *const *argv, sqlite3_vtab **ppVtab,
                          char **pzErr) {
                            vec_static_blob_entriesConnect(db, pAux, argc, argv, ppVtab, pzErr);
                          }

static int vec_static_blob_entriesDisconnect(sqlite3_vtab *pVtab) {
  vec_static_blob_entries_vtab *p = (vec_static_blob_entries_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int vec_static_blob_entriesOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  vec_static_blob_entries_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int vec_static_blob_entriesClose(sqlite3_vtab_cursor *cur) {
  vec_static_blob_entries_cursor *pCur = (vec_static_blob_entries_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int vec_static_blob_entriesBestIndex(sqlite3_vtab *pVTab, sqlite3_index_info *pIdxInfo) {
  vec_static_blob_entries_vtab *p = (vec_static_blob_entries_vtab *)pVTab;
  int iMatchTerm = -1;
  int iLimitTerm = -1;
  int iRowidTerm = -1; // TODO point query
  int iKTerm = -1;

  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    if (!pIdxInfo->aConstraint[i].usable)
      continue;

    int iColumn = pIdxInfo->aConstraint[i].iColumn;
    int op = pIdxInfo->aConstraint[i].op;
    if (op == SQLITE_INDEX_CONSTRAINT_MATCH && iColumn == VEC_STATIC_BLOB_ENTRIES_VECTOR) {
      if (iMatchTerm > -1) {
        // TODO only 1 match operator at a time
        return SQLITE_ERROR;
      }
      iMatchTerm = i;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_LIMIT) {
      iLimitTerm = i;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_EQ && iColumn == VEC_STATIC_BLOB_ENTRIES_K) {
      iKTerm = i;
    }
  }
  if(iMatchTerm >= 0) {
    if (iLimitTerm < 0 && iKTerm < 0) {
      // TODO: error, match on vector1 should require a limit for KNN
      return SQLITE_ERROR;
    }
    if (iLimitTerm >= 0 && iKTerm >= 0) {
      return SQLITE_ERROR; // limit or k, not both
    }
    if (pIdxInfo->nOrderBy < 1) {
      SET_VTAB_ERROR("ORDER BY distance required");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->nOrderBy > 1) {
      // TODO error, orderByConsumed is all or nothing, only 1 order by allowed
      SET_VTAB_ERROR("more than 1 ORDER BY clause provided");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->aOrderBy[0].iColumn != VEC_STATIC_BLOB_ENTRIES_DISTANCE) {
      SET_VTAB_ERROR("ORDER BY must be on the distance column");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->aOrderBy[0].desc) {
      SET_VTAB_ERROR("Only ascending in ORDER BY distance clause is supported, "
                     "DESC is not supported yet.");
      return SQLITE_CONSTRAINT;
    }

    pIdxInfo->idxNum = VEC_SBE__QUERYPLAN_KNN;
    pIdxInfo->estimatedCost = (double)10; // TODO vtab_value(?) as hint?
    pIdxInfo->estimatedRows = 10;// TODO vtab_value(?) as hint?

    pIdxInfo->orderByConsumed = 1;
    pIdxInfo->aConstraintUsage[iMatchTerm].argvIndex = 1;
    pIdxInfo->aConstraintUsage[iMatchTerm].omit = 1;
    if (iLimitTerm >= 0) {
      pIdxInfo->aConstraintUsage[iLimitTerm].argvIndex = 2;
      pIdxInfo->aConstraintUsage[iLimitTerm].omit = 1;
    } else {
      pIdxInfo->aConstraintUsage[iKTerm].argvIndex = 2;
      pIdxInfo->aConstraintUsage[iKTerm].omit = 1;
    }

  }
  else {
    pIdxInfo->idxNum = VEC_SBE__QUERYPLAN_FULLSCAN;
    pIdxInfo->estimatedCost = (double)p->blob->nvectors;
    pIdxInfo->estimatedRows = p->blob->nvectors;
  }
  return SQLITE_OK;
}

static int vec_static_blob_entriesFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                         const char *idxStr, int argc, sqlite3_value **argv) {
  vec_static_blob_entries_cursor *pCur = (vec_static_blob_entries_cursor *)pVtabCursor;
  vec_static_blob_entries_vtab *p = (vec_static_blob_entries_vtab *)pCur->base.pVtab;

  if(idxNum == VEC_SBE__QUERYPLAN_KNN) {
    pCur->query_plan = VEC_SBE__QUERYPLAN_KNN;
    struct vec0_query_knn_data *knn_data =
        sqlite3_malloc(sizeof(struct vec0_query_knn_data));
    if (!knn_data) {
      return SQLITE_NOMEM;
    }
    memset(knn_data, 0, sizeof(struct vec0_query_knn_data));

    void *queryVector;
    size_t dimensions;
    enum VectorElementType elementType;
    vector_cleanup cleanup;
    char *err;
    int rc = vector_from_value(argv[0], &queryVector, &dimensions, &elementType, &cleanup, &err);
    todo_assert(elementType == p->blob->element_type);
    todo_assert(dimensions == p->blob->dimensions);

    #define min(a, b) (((a) < (b)) ? (a) : (b))

    i64 k = min(sqlite3_value_int64(argv[1]), p->blob->nvectors);
    todo_assert(k >= 0);
    if (k == 0) {
      knn_data->k = 0;
      pCur->knn_data = knn_data;
      return SQLITE_OK;
    }

    i32 *topk_rowids = sqlite3_malloc(k * sizeof(i32));
    todo_assert(topk_rowids);
    f32 *distances = sqlite3_malloc(p->blob->nvectors * sizeof(f32));
    todo_assert(distances);

    for(size_t i = 0; i < p->blob->nvectors; i++) {
      float * v = ((float *) p->blob->p) + (i * p->blob->dimensions);
      distances[i] = distance_l2_sqr_float(v,  (float *) queryVector, &p->blob->dimensions);
    }
    min_idx(distances, k, topk_rowids, k);
    knn_data->current_idx = 0;
    knn_data->distances = distances;
    knn_data->k = k;
    knn_data->rowids = topk_rowids;

    pCur->knn_data = knn_data;
  }
  else {
    pCur->query_plan = VEC_SBE__QUERYPLAN_FULLSCAN;
    pCur->iRowid = 0;
  }

  return SQLITE_OK;
}

static int vec_static_blob_entriesRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  vec_static_blob_entries_cursor *pCur = (vec_static_blob_entries_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

static int vec_static_blob_entriesNext(sqlite3_vtab_cursor *cur) {
  vec_static_blob_entries_cursor *pCur = (vec_static_blob_entries_cursor *)cur;
  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      pCur->iRowid++;
      return SQLITE_OK;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      pCur->knn_data->current_idx++;
      return SQLITE_OK;
    }
  }

}

static int vec_static_blob_entriesEof(sqlite3_vtab_cursor *cur) {
  vec_static_blob_entries_cursor *pCur = (vec_static_blob_entries_cursor *)cur;
  vec_static_blob_entries_vtab * p = (vec_static_blob_entries_vtab *) pCur->base.pVtab;
  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      return (size_t) pCur->iRowid >= p->blob->nvectors;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      return pCur->knn_data->current_idx >= pCur->knn_data->k;
    }
  }

}

static int vec_static_blob_entriesColumn(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                         int i) {
  vec_static_blob_entries_cursor *pCur = (vec_static_blob_entries_cursor *)cur;
  vec_static_blob_entries_vtab *p = (vec_static_blob_entries_vtab *)cur->pVtab;

  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      switch (i) {
      case VEC_STATIC_BLOB_ENTRIES_VECTOR:

        sqlite3_result_blob(
          context,
          p->blob->p + (pCur->iRowid * p->blob->dimensions * sizeof(float)),
          p->blob->dimensions * sizeof(float),
          SQLITE_STATIC
        );
        sqlite3_result_subtype(context,p->blob->element_type);
        break;
      }
      return SQLITE_OK;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      switch (i) {
      case VEC_STATIC_BLOB_ENTRIES_VECTOR: {
        i32 rowid = ((i32 *) pCur->knn_data->rowids)[pCur->knn_data->current_idx];

        sqlite3_result_blob(
          context,
          p->blob->p + (rowid* p->blob->dimensions * sizeof(float)),
          p->blob->dimensions * sizeof(float),
          SQLITE_STATIC
          );
        sqlite3_result_subtype(context,p->blob->element_type);
        break;
      }
      }
      return SQLITE_OK;
    }
  }
}


static sqlite3_module vec_static_blob_entriesModule = {
    /* iVersion    */ 3,
    /* xCreate     */ vec_static_blob_entriesCreate,
    /* xConnect    */ vec_static_blob_entriesConnect,
    /* xBestIndex  */ vec_static_blob_entriesBestIndex,
    /* xDisconnect */ vec_static_blob_entriesDisconnect,
    /* xDestroy    */ vec_static_blob_entriesDisconnect,
    /* xOpen       */ vec_static_blob_entriesOpen,
    /* xClose      */ vec_static_blob_entriesClose,
    /* xFilter     */ vec_static_blob_entriesFilter,
    /* xNext       */ vec_static_blob_entriesNext,
    /* xEof        */ vec_static_blob_entriesEof,
    /* xColumn     */ vec_static_blob_entriesColumn,
    /* xRowid      */ vec_static_blob_entriesRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0};
#pragma endregion

#pragma region vec_expo() table function

void dethrone2(int k, f32 *base_distances, i64 *base_rowids, size_t chunk_size,
              i32 *chunk_top_idx, f32 *chunk_distances, i64 *chunk_rowids,

              i64 **out_rowids, f32 **out_distances) {
  *out_rowids = sqlite3_malloc(k * sizeof(i64));
  todo_assert(*out_rowids);
  *out_distances = sqlite3_malloc(k * sizeof(f32));
  todo_assert(*out_distances);

  size_t ptrA = 0;
  size_t ptrB = 0;
  for (int i = 0; i < k; i++) {
    if (ptrA < chunk_size && (ptrB >= k || chunk_distances[chunk_top_idx[ptrA]] < base_distances[ptrB])) {
      (*out_rowids)[i] = chunk_rowids[chunk_top_idx[ptrA]];
      (*out_distances)[i] = chunk_distances[chunk_top_idx[ptrA]];
      ptrA++;
    } else if (ptrB < k) {
      (*out_rowids)[i] = base_rowids[ptrB];
      (*out_distances)[i] = base_distances[ptrB];
      ptrB++;
    }
  }
}


typedef struct vec_expo_vtab vec_expo_vtab;
struct vec_expo_vtab {
  sqlite3_vtab base;
  sqlite3 * db;
  char * table;
  char * column;
};

typedef struct vec_expo_cursor vec_expo_cursor;
struct vec_expo_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
  vec_sbe_query_plan query_plan;
  struct vec0_query_knn_data * knn_data;
};


static int vec_expoConnect(sqlite3 *db, void *pAux, int argc,
                          const char *const *argv, sqlite3_vtab **ppVtab,
                          char **pzErr) {
  vec_expo_vtab *pNew;
  assert(argc==5);

#define VEC_EXPO_VECTOR   0
#define VEC_EXPO_DISTANCE 1
#define VEC_EXPO_K        2
  int rc = sqlite3_declare_vtab(
      db, "CREATE TABLE x(vector, distance hidden, k hidden)");
  assert(rc == SQLITE_OK);
  pNew = sqlite3_malloc(sizeof(*pNew));
  if (pNew == 0)
    return SQLITE_NOMEM;
  memset(pNew, 0, sizeof(*pNew));
  pNew->db = db;
  pNew->table = sqlite3_mprintf("%s", argv[3]);
  pNew->column = sqlite3_mprintf("%s", argv[4]);
  *ppVtab = (sqlite3_vtab *)pNew;
  return SQLITE_OK;
}

static int vec_expoCreate(sqlite3 *db, void *pAux, int argc,
                          const char *const *argv, sqlite3_vtab **ppVtab,
                          char **pzErr) {
                            return vec_expoConnect(db, pAux, argc, argv, ppVtab, pzErr);
                          }

static int vec_expoDisconnect(sqlite3_vtab *pVtab) {
  vec_expo_vtab *p = (vec_expo_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int vec_expoOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor) {
  vec_expo_cursor *pCur;
  pCur = sqlite3_malloc(sizeof(*pCur));
  if (pCur == 0)
    return SQLITE_NOMEM;
  memset(pCur, 0, sizeof(*pCur));
  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static int vec_expoClose(sqlite3_vtab_cursor *cur) {
  vec_expo_cursor *pCur = (vec_expo_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int vec_expoBestIndex(sqlite3_vtab *pVTab, sqlite3_index_info *pIdxInfo) {
  vec_expo_vtab *p = (vec_expo_vtab *)pVTab;
  int iMatchTerm = -1;
  int iLimitTerm = -1;
  int iRowidTerm = -1; // TODO point query
  int iKTerm = -1;

  for (int i = 0; i < pIdxInfo->nConstraint; i++) {
    if (!pIdxInfo->aConstraint[i].usable)
      continue;

    int iColumn = pIdxInfo->aConstraint[i].iColumn;
    int op = pIdxInfo->aConstraint[i].op;
    if (op == SQLITE_INDEX_CONSTRAINT_MATCH && iColumn == VEC_EXPO_VECTOR) {
      if (iMatchTerm > -1) {
        // TODO only 1 match operator at a time
        return SQLITE_ERROR;
      }
      iMatchTerm = i;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_LIMIT) {
      iLimitTerm = i;
    }
    if (op == SQLITE_INDEX_CONSTRAINT_EQ && iColumn == VEC_EXPO_K) {
      iKTerm = i;
    }
  }
  if(iMatchTerm >= 0) {
    if (iLimitTerm < 0 && iKTerm < 0) {
      // TODO: error, match on vector1 should require a limit for KNN
      return SQLITE_ERROR;
    }
    if (iLimitTerm >= 0 && iKTerm >= 0) {
      return SQLITE_ERROR; // limit or k, not both
    }
    if (pIdxInfo->nOrderBy < 1) {
      SET_VTAB_ERROR("ORDER BY distance required");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->nOrderBy > 1) {
      // TODO error, orderByConsumed is all or nothing, only 1 order by allowed
      SET_VTAB_ERROR("more than 1 ORDER BY clause provided");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->aOrderBy[0].iColumn != VEC_EXPO_DISTANCE) {
      SET_VTAB_ERROR("ORDER BY must be on the distance column");
      return SQLITE_CONSTRAINT;
    }
    if (pIdxInfo->aOrderBy[0].desc) {
      SET_VTAB_ERROR("Only ascending in ORDER BY distance clause is supported, "
                     "DESC is not supported yet.");
      return SQLITE_CONSTRAINT;
    }

    pIdxInfo->idxNum = VEC_SBE__QUERYPLAN_KNN;
    pIdxInfo->estimatedCost = (double)10; // TODO vtab_value(?) as hint?
    pIdxInfo->estimatedRows = 10;// TODO vtab_value(?) as hint?

    pIdxInfo->orderByConsumed = 1;
    pIdxInfo->aConstraintUsage[iMatchTerm].argvIndex = 1;
    pIdxInfo->aConstraintUsage[iMatchTerm].omit = 1;
    if (iLimitTerm >= 0) {
      pIdxInfo->aConstraintUsage[iLimitTerm].argvIndex = 2;
      pIdxInfo->aConstraintUsage[iLimitTerm].omit = 1;
    } else {
      pIdxInfo->aConstraintUsage[iKTerm].argvIndex = 2;
      pIdxInfo->aConstraintUsage[iKTerm].omit = 1;
    }

  }
  else {
    pIdxInfo->idxNum = VEC_SBE__QUERYPLAN_FULLSCAN;
    pIdxInfo->estimatedCost = 10000.0;
    pIdxInfo->estimatedRows = 10000;
  }
  return SQLITE_OK;
}

static int vec_expoFilter(sqlite3_vtab_cursor *pVtabCursor, int idxNum,
                         const char *idxStr, int argc, sqlite3_value **argv) {
  vec_expo_cursor *pCur = (vec_expo_cursor *)pVtabCursor;
  vec_expo_vtab *p = (vec_expo_vtab *)pCur->base.pVtab;

  if(idxNum == VEC_SBE__QUERYPLAN_KNN) {
    pCur->query_plan = VEC_SBE__QUERYPLAN_KNN;
    struct vec0_query_knn_data *knn_data =
        sqlite3_malloc(sizeof(struct vec0_query_knn_data));
    if (!knn_data) {
      return SQLITE_NOMEM;
    }
    memset(knn_data, 0, sizeof(struct vec0_query_knn_data));

    void *queryVector;
    size_t dimensions;
    enum VectorElementType elementType;
    vector_cleanup cleanup;
    char *err;
    int rc = vector_from_value(argv[0], &queryVector, &dimensions, &elementType,
                          &cleanup, &err);
    todo_assert(elementType == SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
    todo_assert(dimensions > 0);

    i64 k = sqlite3_value_int64(argv[1]);
    todo_assert(k >= 0);
    if (k == 0) {
      knn_data->k = 0;
      pCur->knn_data = knn_data;
      return SQLITE_OK;
    }

  i64 *topk_rowids = sqlite3_malloc(k * sizeof(i64));
  todo_assert(topk_rowids);
  f32 *topk_distances = sqlite3_malloc(k * sizeof(f32));
  todo_assert(topk_distances);

    sqlite3_stmt * stmtRowids;
    char * zSql = sqlite3_mprintf("select rowid from \"%w\" ", p->table);
    assert(zSql);
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtRowids, NULL);
    assert(rc == SQLITE_OK);

    sqlite3_blob * baseVectorsBlob;
    sqlite3_blob_open(p->db, "main", p->table, p->column, 1, 0, &baseVectorsBlob);

    int chunk_size = 200;
    float * chunk = sqlite3_malloc(dimensions * chunk_size * sizeof(float));
    assert(chunk);

    f32 *chunk_distances = sqlite3_malloc(chunk_size * sizeof(f32));
    todo_assert(chunk_distances);
    for (int i = 0; i < k; i++) {
      topk_distances[i] = FLT_MAX;
    }
    i64 *chunk_rowids = sqlite3_malloc(chunk_size * sizeof(i64));
    todo_assert(chunk_rowids);



    while(true) {
      int nused = 0;
      for(int i = 0; i < chunk_size; i++) {
        rc = sqlite3_step(stmtRowids);
        if(rc == SQLITE_DONE) {
          break;
        }
        assert(rc == SQLITE_ROW);
        nused = i+1;

        i64 rowid = sqlite3_column_int64(stmtRowids, 0);
        chunk_rowids[i] = rowid;
        rc = sqlite3_blob_reopen(baseVectorsBlob, rowid);
        assert(rc == SQLITE_OK);
        assert(sqlite3_blob_bytes(baseVectorsBlob) == dimensions * sizeof(float));
        sqlite3_blob_read(baseVectorsBlob, &chunk[i * dimensions], dimensions * sizeof(float), 0);
      }

      for(int i = 0; i < nused; i++) {
        const f32 *base_i = (chunk) + (i * dimensions);
        chunk_distances[i] = distance_l2_sqr_float(base_i, (f32 *)queryVector, &dimensions);
      }

      i32 *chunk_top_idxs = sqlite3_malloc(nused * sizeof(i32));
      todo_assert(chunk_top_idxs);
      min_idx(chunk_distances, nused, chunk_top_idxs, nused);

      i64 *out_rowids;
      f32 *out_distances;
      dethrone2(k, topk_distances, topk_rowids, /*chunk_size*/ nused, chunk_top_idxs,
               chunk_distances, chunk_rowids,

               &out_rowids, &out_distances);
      for (int i = 0; i < k; i++) {
        topk_rowids[i] = out_rowids[i];
        topk_distances[i] = out_distances[i];
      }
      sqlite3_free(out_rowids);
      sqlite3_free(out_distances);
      sqlite3_free(chunk_top_idxs);

      if(nused < chunk_size) break;
    }
    sqlite3_blob_close(baseVectorsBlob);
    sqlite3_finalize(stmtRowids);

    cleanup(queryVector);


    knn_data->current_idx = 0;
    knn_data->k = k;
    knn_data->rowids = topk_rowids;
    knn_data->distances = topk_distances;
    pCur->knn_data = knn_data;
  }
  else {
    pCur->query_plan = VEC_SBE__QUERYPLAN_FULLSCAN;
    pCur->iRowid = 0;
  }

  return SQLITE_OK;
}

static int vec_expoRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid) {
  vec_expo_cursor *pCur = (vec_expo_cursor *)cur;
  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      *pRowid = pCur->iRowid;
      break;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      *pRowid = pCur->knn_data->rowids[pCur->knn_data->current_idx];
      break;
    }
  }

  return SQLITE_OK;
}

static int vec_expoNext(sqlite3_vtab_cursor *cur) {
  vec_expo_cursor *pCur = (vec_expo_cursor *)cur;
  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      pCur->iRowid++;
      return SQLITE_OK;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      pCur->knn_data->current_idx++;
      return SQLITE_OK;
    }
  }

}

static int vec_expoEof(sqlite3_vtab_cursor *cur) {
  vec_expo_cursor *pCur = (vec_expo_cursor *)cur;
  vec_expo_vtab * p = (vec_expo_vtab *) pCur->base.pVtab;
  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      return 1;//(size_t) pCur->iRowid >= p->blob->nvectors;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      return pCur->knn_data->current_idx >= pCur->knn_data->k;
    }
  }

}

static int vec_expoColumn(sqlite3_vtab_cursor *cur, sqlite3_context *context,
                         int i) {
  vec_expo_cursor *pCur = (vec_expo_cursor *)cur;
  vec_expo_vtab *p = (vec_expo_vtab *)cur->pVtab;

  switch(pCur->query_plan) {
    case VEC_SBE__QUERYPLAN_FULLSCAN: {
      return SQLITE_OK;
    }
    case VEC_SBE__QUERYPLAN_KNN: {
      switch(i) {
        case VEC_EXPO_VECTOR: {
          break;
        }
        case VEC_EXPO_DISTANCE: {
          sqlite3_result_double(context, pCur->knn_data->distances[pCur->knn_data->current_idx]);
          break;
        }
      }
      return SQLITE_OK;
    }
  }
}


static sqlite3_module vec_expoModule = {
    /* iVersion    */ 3,
    /* xCreate     */ vec_expoCreate,
    /* xConnect    */ vec_expoConnect,
    /* xBestIndex  */ vec_expoBestIndex,
    /* xDisconnect */ vec_expoDisconnect,
    /* xDestroy    */ vec_expoDisconnect,
    /* xOpen       */ vec_expoOpen,
    /* xClose      */ vec_expoClose,
    /* xFilter     */ vec_expoFilter,
    /* xNext       */ vec_expoNext,
    /* xEof        */ vec_expoEof,
    /* xColumn     */ vec_expoColumn,
    /* xRowid      */ vec_expoRowid,
    /* xUpdate     */ 0,
    /* xBegin      */ 0,
    /* xSync       */ 0,
    /* xCommit     */ 0,
    /* xRollback   */ 0,
    /* xFindMethod */ 0,
    /* xRename     */ 0,
    /* xSavepoint  */ 0,
    /* xRelease    */ 0,
    /* xRollbackTo */ 0,
    /* xShadowName */ 0};
#pragma endregion

#endif

int sqlite3_mmap_warm(sqlite3 *db, const char *zDb) {
  int rc = SQLITE_OK;
  char *zSql = 0;
  int pgsz = 0;
  unsigned int nTotal = 0;

  if (0 == sqlite3_get_autocommit(db))
    return SQLITE_MISUSE;

  /* Open a read-only transaction on the file in question */
  zSql = sqlite3_mprintf("BEGIN; SELECT * FROM %s%q%ssqlite_schema",
                         (zDb ? "'" : ""), (zDb ? zDb : ""), (zDb ? "'." : ""));
  if (zSql == 0)
    return SQLITE_NOMEM;
  rc = sqlite3_exec(db, zSql, 0, 0, 0);
  sqlite3_free(zSql);

  /* Find the SQLite page size of the file */
  if (rc == SQLITE_OK) {
    zSql = sqlite3_mprintf("PRAGMA %s%q%spage_size", (zDb ? "'" : ""),
                           (zDb ? zDb : ""), (zDb ? "'." : ""));
    if (zSql == 0) {
      rc = SQLITE_NOMEM;
    } else {
      sqlite3_stmt *pPgsz = 0;
      rc = sqlite3_prepare_v2(db, zSql, -1, &pPgsz, 0);
      sqlite3_free(zSql);
      if (rc == SQLITE_OK) {
        if (sqlite3_step(pPgsz) == SQLITE_ROW) {
          pgsz = sqlite3_column_int(pPgsz, 0);
        }
        rc = sqlite3_finalize(pPgsz);
      }
      if (rc == SQLITE_OK && pgsz == 0) {
        rc = SQLITE_ERROR;
      }
    }
  }

  /* Touch each mmap'd page of the file */
  if (rc == SQLITE_OK) {
    int rc2;
    sqlite3_file *pFd = 0;
    rc = sqlite3_file_control(db, zDb, SQLITE_FCNTL_FILE_POINTER, &pFd);
    if (rc == SQLITE_OK && pFd->pMethods && pFd->pMethods->iVersion >= 3) {
      i64 iPg = 1;
      sqlite3_io_methods const *p = pFd->pMethods;
      while (1) {
        unsigned char *pMap;
        rc = p->xFetch(pFd, pgsz * iPg, pgsz, (void **)&pMap);
        if (rc != SQLITE_OK || pMap == 0)
          break;

        nTotal += (unsigned int)pMap[0];
        nTotal += (unsigned int)pMap[pgsz - 1];

        rc = p->xUnfetch(pFd, pgsz * iPg, (void *)pMap);
        if (rc != SQLITE_OK)
          break;
        iPg++;
      }
      sqlite3_log(SQLITE_OK,
                  "sqlite3_mmap_warm_cache: Warmed up %d pages of %s",
                  iPg == 1 ? 0 : iPg, sqlite3_db_filename(db, zDb));
    }

    rc2 = sqlite3_exec(db, "END", 0, 0, 0);
    if (rc == SQLITE_OK)
      rc = rc2;
  }

  (void)nTotal;
  return rc;
}

#ifdef _WIN32
__declspec(dllexport)
#endif
    int sqlite3_vec_warm_mmap(sqlite3 *db, char **pzErrMsg,
                              const sqlite3_api_routines *pApi) {
  UNUSED_PARAMETER(pzErrMsg);
  SQLITE_EXTENSION_INIT2(pApi);
  return sqlite3_mmap_warm(db, NULL);
}

#ifdef SQLITE_VEC_ENABLE_AVX
#define SQLITE_VEC_DEBUG_BUILD_AVX "avx"
#else
#define SQLITE_VEC_DEBUG_BUILD_AVX ""
#endif
#ifdef SQLITE_VEC_ENABLE_NEON
#define SQLITE_VEC_DEBUG_BUILD_NEON "neon"
#else
#define SQLITE_VEC_DEBUG_BUILD_NEON ""
#endif

#define SQLITE_VEC_DEBUG_BUILD                                                 \
  SQLITE_VEC_DEBUG_BUILD_AVX " " SQLITE_VEC_DEBUG_BUILD_NEON

#define SQLITE_VEC_DEBUG_STRING                                                \
  "Version: " SQLITE_VEC_VERSION "\n"                                          \
  "Date: " SQLITE_VEC_DATE "\n"                                                \
  "Commit: " SQLITE_VEC_SOURCE "\n"                                            \
  "Build flags: " SQLITE_VEC_DEBUG_BUILD

#ifndef SQLITE_SUBTYPE
#define SQLITE_SUBTYPE 0x000100000
#endif

#ifndef SQLITE_RESULT_SUBTYPE
#define SQLITE_RESULT_SUBTYPE 0x001000000
#endif

    int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg,
                         const sqlite3_api_routines *pApi) {
  SQLITE_EXTENSION_INIT2(pApi);
  int rc = SQLITE_OK;
  const int DEFAULT_FLAGS =
      SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC;

  static const struct {
    char *zFName;
    void (*xFunc)(sqlite3_context *, int, sqlite3_value **);
    int nArg;
    int flags;
    void *p;
  } aFunc[] = {
      // clang-format off
    {"vec_version",         _static_text_func,    0, DEFAULT_FLAGS,                                          SQLITE_VEC_VERSION },
    {"vec_debug",           _static_text_func,    0, DEFAULT_FLAGS,                                          SQLITE_VEC_DEBUG_STRING },
    {"vec_distance_l2",     vec_distance_l2,      2, DEFAULT_FLAGS | SQLITE_SUBTYPE,                         NULL },
    {"vec_distance_hamming",vec_distance_hamming, 2, DEFAULT_FLAGS | SQLITE_SUBTYPE,                         NULL },
    {"vec_distance_cosine", vec_distance_cosine,  2, DEFAULT_FLAGS | SQLITE_SUBTYPE,                         NULL },
    {"vec_length",          vec_length,           1, DEFAULT_FLAGS | SQLITE_SUBTYPE,                         NULL },
    {"vec_to_json",         vec_to_json,          1, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_add",             vec_add,              2, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_sub",             vec_sub,              2, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_slice",           vec_slice,            3, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_normalize",       vec_normalize,        1, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_f32",             vec_f32,              1, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_bit",             vec_bit,              1, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_int8",            vec_int8,             1, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_quantize_i8",     vec_quantize_i8,      2, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_quantize_i8",     vec_quantize_i8,      3, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    {"vec_quantize_binary", vec_quantize_binary,  1, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    #ifdef SQLITE_VEC_ENABLE_EXPERIMENTAL
    {"vec_static_blob_from_raw", vec_static_blob_from_raw,  4, DEFAULT_FLAGS | SQLITE_SUBTYPE | SQLITE_RESULT_SUBTYPE, NULL },
    #endif
      // clang-format on
  };

  #ifdef SQLITE_VEC_ENABLE_EXPERIMENTAL
  vec_static_blob_data * static_blob_data;
  static_blob_data = sqlite3_malloc(sizeof(*static_blob_data));
  todo_assert(static_blob_data);
  memset(static_blob_data, 0, sizeof(*static_blob_data));
  #endif

  static const struct {
    char *name;
    const sqlite3_module *module;
    void * p;
    void(*xDestroy)(void*);
  } aMod[] = {
      // clang-format off
    {"vec0",          &vec0Module,          NULL, NULL},
    {"vec_each",      &vec_eachModule,      NULL, NULL},
    {"vec_npy_each",  &vec_npy_eachModule,  NULL, NULL},
      // clang-format on
  };

  for (unsigned long i = 0;
       i < sizeof(aFunc) / sizeof(aFunc[0]) && rc == SQLITE_OK; i++) {
    rc = sqlite3_create_function_v2(db, aFunc[i].zFName, aFunc[i].nArg,
                                    aFunc[i].flags, aFunc[i].p, aFunc[i].xFunc,
                                    NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("Error creating function %s: %s",
                                  aFunc[i].zFName, sqlite3_errmsg(db));
      return rc;
    }
  }

  for (unsigned long i = 0; i < countof(aMod) && rc == SQLITE_OK; i++) {
    rc = sqlite3_create_module_v2(db, aMod[i].name, aMod[i].module, NULL, NULL);
    if (rc != SQLITE_OK) {
      *pzErrMsg = sqlite3_mprintf("Error creating module %s: %s", aMod[i].name,
                                  sqlite3_errmsg(db));
      return rc;
    }
  }
  #ifdef SQLITE_VEC_ENABLE_EXPERIMENTAL
  rc = sqlite3_create_module_v2(db, "vec_static_blobs", &vec_static_blobsModule, static_blob_data, sqlite3_free);
  assert(rc == SQLITE_OK);
  rc = sqlite3_create_module_v2(db, "vec_static_blob_entries", &vec_static_blob_entriesModule, static_blob_data, NULL);
  assert(rc == SQLITE_OK);
  rc = sqlite3_create_module_v2(db, "vec_expo", &vec_expoModule, NULL, NULL);
  assert(rc == SQLITE_OK);
  #endif

  return SQLITE_OK;
}

    int sqlite3_vec_fs_read_init(sqlite3 *db, char **pzErrMsg,
                                 const sqlite3_api_routines *pApi) {
  UNUSED_PARAMETER(pzErrMsg);
  SQLITE_EXTENSION_INIT2(pApi);
  int rc = SQLITE_OK;
  rc = sqlite3_create_function_v2(db, "vec_npy_file", 1, SQLITE_RESULT_SUBTYPE,
                                  NULL, vec_npy_file, NULL, NULL, NULL);
  return rc;
}

#ifdef SQLITE_VEC_ENABLE_TRACE_ENTRYPOINT

int trace(unsigned int x, void *p1, void *p2, void *p3) {
  if (x == SQLITE_TRACE_STMT) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)p2;
    char *zSql = sqlite3_expanded_sql(stmt);
    printf("%s\n", zSql);
  }
}
#ifdef _WIN32
__declspec(dllexport)
#endif
    int trace_debug(sqlite3 *db, char **pzErrMsg,
                    const sqlite3_api_routines *pApi) {
  UNUSED_PARAMETER(pzErrMsg);
  SQLITE_EXTENSION_INIT2(pApi);
  sqlite3_trace_v2(db, SQLITE_TRACE_STMT, trace, NULL);
  return SQLITE_OK;
}
#endif
