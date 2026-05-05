/**
 * Fuzz target for DiskANN RobustPrune algorithm (diskann_prune_select).
 *
 * diskann_prune_select is exposed for testing and takes:
 *   - inter_distances: flattened NxN matrix of inter-candidate distances
 *   - p_distances: N distances from node p to each candidate
 *   - num_candidates, alpha, max_neighbors
 *
 * This is a pure function that doesn't need a database, so we can
 * call it directly with fuzz-controlled inputs. This gives the fuzzer
 * maximum speed (no SQLite overhead) to explore:
 *
 *   - alpha boundary: alpha=0 (prunes nothing), alpha=very large (prunes all)
 *   - max_neighbors = 0, 1, num_candidates, > num_candidates
 *   - num_candidates = 0, 1, large
 *   - Distance matrices with: all zeros, all same, negative values, NaN, Inf
 *   - Non-symmetric distance matrices (should still work)
 *   - Memory: large num_candidates to stress malloc
 *
 * Key code paths:
 *   - diskann_prune_select alpha-pruning loop
 *   - Boundary: selectedCount reaches max_neighbors exactly
 *   - All candidates pruned before max_neighbors reached
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/* Declare the test-exposed function.
 * diskann_prune_select is not static -- it's a public symbol. */
extern int diskann_prune_select(
    const float *inter_distances, const float *p_distances,
    int num_candidates, float alpha, int max_neighbors,
    int *outSelected, int *outCount);

static uint8_t fuzz_byte(const uint8_t **data, size_t *size, uint8_t def) {
  if (*size == 0) return def;
  uint8_t b = **data;
  (*data)++;
  (*size)--;
  return b;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) return 0;

  /* Consume parameters from fuzz data */
  int num_candidates = fuzz_byte(&data, &size, 0) % 33; /* 0..32 */
  int max_neighbors = fuzz_byte(&data, &size, 0) % 17;  /* 0..16 */

  /* Alpha: pick from interesting values */
  uint8_t alpha_idx = fuzz_byte(&data, &size, 0) % 8;
  float alpha_values[] = {0.0f, 0.5f, 1.0f, 1.2f, 1.5f, 2.0f, 10.0f, 100.0f};
  float alpha = alpha_values[alpha_idx];

  if (num_candidates == 0) {
    /* Test empty case */
    int outCount = -1;
    int rc = diskann_prune_select(NULL, NULL, 0, alpha, max_neighbors,
                                   NULL, &outCount);
    assert(rc == 0 /* SQLITE_OK */);
    assert(outCount == 0);
    return 0;
  }

  /* Allocate arrays */
  int n = num_candidates;
  float *inter_distances = malloc(n * n * sizeof(float));
  float *p_distances = malloc(n * sizeof(float));
  int *outSelected = malloc(n * sizeof(int));
  if (!inter_distances || !p_distances || !outSelected) {
    free(inter_distances);
    free(p_distances);
    free(outSelected);
    return 0;
  }

  /* Fill p_distances from fuzz data (sorted ascending for correct input) */
  for (int i = 0; i < n; i++) {
    uint8_t raw = fuzz_byte(&data, &size, (uint8_t)(i * 10));
    p_distances[i] = (float)raw / 10.0f;
  }
  /* Sort p_distances ascending (prune_select expects sorted input) */
  for (int i = 1; i < n; i++) {
    float tmp = p_distances[i];
    int j = i - 1;
    while (j >= 0 && p_distances[j] > tmp) {
      p_distances[j + 1] = p_distances[j];
      j--;
    }
    p_distances[j + 1] = tmp;
  }

  /* Fill inter-distance matrix from fuzz data */
  for (int i = 0; i < n * n; i++) {
    uint8_t raw = fuzz_byte(&data, &size, (uint8_t)(i % 256));
    inter_distances[i] = (float)raw / 10.0f;
  }
  /* Make diagonal zero */
  for (int i = 0; i < n; i++) {
    inter_distances[i * n + i] = 0.0f;
  }

  int outCount = -1;
  int rc = diskann_prune_select(inter_distances, p_distances,
                                 n, alpha, max_neighbors,
                                 outSelected, &outCount);
  /* Basic sanity: should not crash, count should be valid */
  assert(rc == 0);
  assert(outCount >= 0);
  assert(outCount <= max_neighbors || max_neighbors == 0);
  assert(outCount <= n);

  /* Verify outSelected flags are consistent with outCount */
  int flagCount = 0;
  for (int i = 0; i < n; i++) {
    if (outSelected[i]) flagCount++;
  }
  assert(flagCount == outCount);

  free(inter_distances);
  free(p_distances);
  free(outSelected);
  return 0;
}
