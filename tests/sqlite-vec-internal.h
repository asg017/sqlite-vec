#ifndef SQLITE_VEC_INTERNAL_H
#define SQLITE_VEC_INTERNAL_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

int min_idx(
  const float *distances,
  int32_t n,
  uint8_t *candidates,
  int32_t *out,
  int32_t k,
  uint8_t *bTaken,
  int32_t *k_used
);

// Scanner / tokenizer types and functions

enum Vec0TokenType {
  TOKEN_TYPE_IDENTIFIER = 0,
  TOKEN_TYPE_DIGIT = 1,
  TOKEN_TYPE_LBRACKET = 2,
  TOKEN_TYPE_RBRACKET = 3,
  TOKEN_TYPE_PLUS = 4,
  TOKEN_TYPE_EQ = 5,
  TOKEN_TYPE_LPAREN = 6,
  TOKEN_TYPE_RPAREN = 7,
  TOKEN_TYPE_COMMA = 8,
};

#define VEC0_TOKEN_RESULT_EOF   1
#define VEC0_TOKEN_RESULT_SOME  2
#define VEC0_TOKEN_RESULT_ERROR 3

struct Vec0Token {
  enum Vec0TokenType token_type;
  char *start;
  char *end;
};

struct Vec0Scanner {
  char *start;
  char *end;
  char *ptr;
};

void vec0_scanner_init(struct Vec0Scanner *scanner, const char *source, int source_length);
int vec0_scanner_next(struct Vec0Scanner *scanner, struct Vec0Token *out);
int vec0_token_next(char *start, char *end, struct Vec0Token *out);

// Vector column definition types and parser

enum VectorElementType {
  SQLITE_VEC_ELEMENT_TYPE_FLOAT32 = 223 + 0,
  SQLITE_VEC_ELEMENT_TYPE_BIT     = 223 + 1,
  SQLITE_VEC_ELEMENT_TYPE_INT8    = 223 + 2,
};

enum Vec0DistanceMetrics {
  VEC0_DISTANCE_METRIC_L2 = 1,
  VEC0_DISTANCE_METRIC_COSINE = 2,
  VEC0_DISTANCE_METRIC_L1 = 3,
};

enum Vec0IndexType {
  VEC0_INDEX_TYPE_FLAT = 1,
};

struct VectorColumnDefinition {
  char *name;
  int name_length;
  size_t dimensions;
  enum VectorElementType element_type;
  enum Vec0DistanceMetrics distance_metric;
  enum Vec0IndexType index_type;
};

int vec0_parse_vector_column(const char *source, int source_length,
                             struct VectorColumnDefinition *outColumn);

int vec0_parse_partition_key_definition(const char *source, int source_length,
                                        char **out_column_name,
                                        int *out_column_name_length,
                                        int *out_column_type);

#ifdef SQLITE_VEC_TEST
float _test_distance_l2_sqr_float(const float *a, const float *b, size_t dims);
float _test_distance_cosine_float(const float *a, const float *b, size_t dims);
float _test_distance_hamming(const unsigned char *a, const unsigned char *b, size_t dims);
#endif

#endif /* SQLITE_VEC_INTERNAL_H */
