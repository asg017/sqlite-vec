#ifndef SQLITE_VEC_INTERNAL_H
#define SQLITE_VEC_INTERNAL_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#ifndef SQLITE_VEC_ENABLE_IVF
#define SQLITE_VEC_ENABLE_IVF 1
#endif

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
#ifdef SQLITE_VEC_ENABLE_RESCORE
  VEC0_INDEX_TYPE_RESCORE = 2,
#endif
  VEC0_INDEX_TYPE_IVF = 3,
  VEC0_INDEX_TYPE_DISKANN = 4,
};

enum Vec0RescoreQuantizerType {
  VEC0_RESCORE_QUANTIZER_BIT = 1,
  VEC0_RESCORE_QUANTIZER_INT8 = 2,
};

struct Vec0RescoreConfig {
  enum Vec0RescoreQuantizerType quantizer_type;
  int oversample;
};

#if SQLITE_VEC_ENABLE_IVF
enum Vec0IvfQuantizer {
  VEC0_IVF_QUANTIZER_NONE = 0,
  VEC0_IVF_QUANTIZER_INT8 = 1,
  VEC0_IVF_QUANTIZER_BINARY = 2,
};

struct Vec0IvfConfig {
  int nlist;
  int nprobe;
  int quantizer;
  int oversample;
};
#else
struct Vec0IvfConfig { char _unused; };
#endif

#ifdef SQLITE_VEC_ENABLE_RESCORE
enum Vec0RescoreQuantizerType {
  VEC0_RESCORE_QUANTIZER_BIT = 1,
  VEC0_RESCORE_QUANTIZER_INT8 = 2,
};

struct Vec0RescoreConfig {
  enum Vec0RescoreQuantizerType quantizer_type;
  int oversample;
};
#endif

enum Vec0DiskannQuantizerType {
  VEC0_DISKANN_QUANTIZER_BINARY = 1,
  VEC0_DISKANN_QUANTIZER_INT8   = 2,
};

struct Vec0DiskannConfig {
  enum Vec0DiskannQuantizerType quantizer_type;
  int n_neighbors;
  int search_list_size;
  int search_list_size_search;
  int search_list_size_insert;
  float alpha;
  int buffer_threshold;
};

struct VectorColumnDefinition {
  char *name;
  int name_length;
  size_t dimensions;
  enum VectorElementType element_type;
  enum Vec0DistanceMetrics distance_metric;
  enum Vec0IndexType index_type;
#ifdef SQLITE_VEC_ENABLE_RESCORE
  struct Vec0RescoreConfig rescore;
#endif
  struct Vec0IvfConfig ivf;
  struct Vec0DiskannConfig diskann;
};

int vec0_parse_vector_column(const char *source, int source_length,
                             struct VectorColumnDefinition *outColumn);

int vec0_parse_partition_key_definition(const char *source, int source_length,
                                        char **out_column_name,
                                        int *out_column_name_length,
                                        int *out_column_type);

size_t diskann_quantized_vector_byte_size(
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions);

int diskann_validity_byte_size(int n_neighbors);
size_t diskann_neighbor_ids_byte_size(int n_neighbors);
size_t diskann_neighbor_qvecs_byte_size(
    int n_neighbors, enum Vec0DiskannQuantizerType quantizer_type,
    size_t dimensions);
int diskann_node_init(
    int n_neighbors, enum Vec0DiskannQuantizerType quantizer_type,
    size_t dimensions,
    unsigned char **outValidity, int *outValiditySize,
    unsigned char **outNeighborIds, int *outNeighborIdsSize,
    unsigned char **outNeighborQvecs, int *outNeighborQvecsSize);
int diskann_validity_get(const unsigned char *validity, int i);
void diskann_validity_set(unsigned char *validity, int i, int value);
int diskann_validity_count(const unsigned char *validity, int n_neighbors);
long long diskann_neighbor_id_get(const unsigned char *neighbor_ids, int i);
void diskann_neighbor_id_set(unsigned char *neighbor_ids, int i, long long rowid);
const unsigned char *diskann_neighbor_qvec_get(
    const unsigned char *qvecs, int i,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions);
void diskann_neighbor_qvec_set(
    unsigned char *qvecs, int i, const unsigned char *src_qvec,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions);
void diskann_node_set_neighbor(
    unsigned char *validity, unsigned char *neighbor_ids, unsigned char *qvecs, int i,
    long long neighbor_rowid, const unsigned char *neighbor_qvec,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions);
void diskann_node_clear_neighbor(
    unsigned char *validity, unsigned char *neighbor_ids, unsigned char *qvecs, int i,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions);
int diskann_quantize_vector(
    const float *src, size_t dimensions,
    enum Vec0DiskannQuantizerType quantizer_type,
    unsigned char *out);

int diskann_prune_select(
    const float *inter_distances, const float *p_distances,
    int num_candidates, float alpha, int max_neighbors,
    int *outSelected, int *outCount);

#ifdef SQLITE_VEC_TEST
float _test_distance_l2_sqr_float(const float *a, const float *b, size_t dims);
float _test_distance_cosine_float(const float *a, const float *b, size_t dims);
float _test_distance_hamming(const unsigned char *a, const unsigned char *b, size_t dims);

#ifdef SQLITE_VEC_ENABLE_RESCORE
void _test_rescore_quantize_float_to_bit(const float *src, uint8_t *dst, size_t dim);
void _test_rescore_quantize_float_to_int8(const float *src, int8_t *dst, size_t dim);
size_t _test_rescore_quantized_byte_size_bit(size_t dimensions);
size_t _test_rescore_quantized_byte_size_int8(size_t dimensions);
#endif
#if SQLITE_VEC_ENABLE_IVF
void ivf_quantize_int8(const float *src, int8_t *dst, int D);
void ivf_quantize_binary(const float *src, uint8_t *dst, int D);
#endif
// DiskANN candidate list (opaque struct, use accessors)
struct DiskannCandidateList {
  void *items;  // opaque
  int count;
  int capacity;
};

int _test_diskann_candidate_list_init(struct DiskannCandidateList *list, int capacity);
void _test_diskann_candidate_list_free(struct DiskannCandidateList *list);
int _test_diskann_candidate_list_insert(struct DiskannCandidateList *list, long long rowid, float distance);
int _test_diskann_candidate_list_next_unvisited(const struct DiskannCandidateList *list);
int _test_diskann_candidate_list_count(const struct DiskannCandidateList *list);
long long _test_diskann_candidate_list_rowid(const struct DiskannCandidateList *list, int i);
float _test_diskann_candidate_list_distance(const struct DiskannCandidateList *list, int i);
void _test_diskann_candidate_list_set_visited(struct DiskannCandidateList *list, int i);

// DiskANN visited set (opaque struct, use accessors)
struct DiskannVisitedSet {
  void *slots;  // opaque
  int capacity;
  int count;
};

int _test_diskann_visited_set_init(struct DiskannVisitedSet *set, int capacity);
void _test_diskann_visited_set_free(struct DiskannVisitedSet *set);
int _test_diskann_visited_set_contains(const struct DiskannVisitedSet *set, long long rowid);
int _test_diskann_visited_set_insert(struct DiskannVisitedSet *set, long long rowid);
#endif

#endif /* SQLITE_VEC_INTERNAL_H */
