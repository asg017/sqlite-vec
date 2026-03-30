/**
 * sqlite-vec-ivf-kmeans.c — Pure k-means clustering algorithm.
 *
 * No SQLite dependency. Operates on float arrays in memory.
 * #include'd into sqlite-vec.c after struct definitions.
 */

#ifndef SQLITE_VEC_IVF_KMEANS_C
#define SQLITE_VEC_IVF_KMEANS_C

// When opened standalone in an editor, pull in types so the LSP is happy.
// When #include'd from sqlite-vec.c, SQLITE_VEC_H is already defined.
#ifndef SQLITE_VEC_H
#include "sqlite-vec.c" // IWYU pragma: keep
#endif

#include <float.h>
#include <string.h>

#define VEC0_IVF_KMEANS_MAX_ITER     25
#define VEC0_IVF_KMEANS_DEFAULT_SEED  0

// Simple xorshift32 PRNG
static uint32_t ivf_xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

// L2 squared distance between two float vectors
static float ivf_l2_dist(const float *a, const float *b, int D) {
  float sum = 0.0f;
  for (int d = 0; d < D; d++) {
    float diff = a[d] - b[d];
    sum += diff * diff;
  }
  return sum;
}

// Find nearest centroid for a single vector. Returns centroid index.
static int ivf_nearest_centroid(const float *vec, const float *centroids,
                                int D, int k) {
  float min_dist = FLT_MAX;
  int best = 0;
  for (int c = 0; c < k; c++) {
    float dist = ivf_l2_dist(vec, &centroids[c * D], D);
    if (dist < min_dist) {
      min_dist = dist;
      best = c;
    }
  }
  return best;
}

/**
 * K-means++ initialization.
 * Picks k initial centroids from the data with probability proportional
 * to squared distance from nearest existing centroid.
 */
static int ivf_kmeans_init_plusplus(const float *vectors, int N, int D,
                                    int k, uint32_t seed, float *centroids) {
  if (N <= 0 || k <= 0 || D <= 0)
    return -1;
  if (seed == 0)
    seed = 42;

  // Pick first centroid randomly
  int first = ivf_xorshift32(&seed) % N;
  memcpy(centroids, &vectors[first * D], D * sizeof(float));

  if (k == 1)
    return 0;

  // Allocate distance array
  float *dists = sqlite3_malloc64((i64)N * sizeof(float));
  if (!dists)
    return -1;

  for (int c = 1; c < k; c++) {
    // Compute D(x) = distance to nearest existing centroid
    double total = 0.0;
    for (int i = 0; i < N; i++) {
      float d = ivf_l2_dist(&vectors[i * D], &centroids[(c - 1) * D], D);
      if (c == 1 || d < dists[i]) {
        dists[i] = d;
      }
      total += dists[i];
    }

    // Weighted random selection
    if (total <= 0.0) {
      // All distances zero — pick randomly
      int pick = ivf_xorshift32(&seed) % N;
      memcpy(&centroids[c * D], &vectors[pick * D], D * sizeof(float));
    } else {
      double threshold = ((double)ivf_xorshift32(&seed) / (double)0xFFFFFFFF) * total;
      double cumulative = 0.0;
      int pick = N - 1;
      for (int i = 0; i < N; i++) {
        cumulative += dists[i];
        if (cumulative >= threshold) {
          pick = i;
          break;
        }
      }
      memcpy(&centroids[c * D], &vectors[pick * D], D * sizeof(float));
    }
  }

  sqlite3_free(dists);
  return 0;
}

/**
 * Lloyd's k-means algorithm.
 *
 * @param vectors   N*D float array (row-major)
 * @param N         number of vectors
 * @param D         dimensionality
 * @param k         number of clusters
 * @param max_iter  maximum iterations
 * @param seed      PRNG seed for initialization
 * @param out_centroids  output: k*D float array (caller-allocated)
 * @return 0 on success, -1 on error
 */
static int ivf_kmeans(const float *vectors, int N, int D, int k,
                       int max_iter, uint32_t seed, float *out_centroids) {
  if (N <= 0 || D <= 0 || k <= 0)
    return -1;

  // Clamp k to N
  if (k > N)
    k = N;

  // Allocate working memory
  int *assignments = sqlite3_malloc64((i64)N * sizeof(int));
  float *new_centroids = sqlite3_malloc64((i64)k * D * sizeof(float));
  int *counts = sqlite3_malloc64((i64)k * sizeof(int));

  if (!assignments || !new_centroids || !counts) {
    sqlite3_free(assignments);
    sqlite3_free(new_centroids);
    sqlite3_free(counts);
    return -1;
  }

  memset(assignments, -1, N * sizeof(int));

  // Initialize centroids via k-means++
  if (ivf_kmeans_init_plusplus(vectors, N, D, k, seed, out_centroids) != 0) {
    sqlite3_free(assignments);
    sqlite3_free(new_centroids);
    sqlite3_free(counts);
    return -1;
  }

  for (int iter = 0; iter < max_iter; iter++) {
    // Assignment step
    int changed = 0;
    for (int i = 0; i < N; i++) {
      int nearest = ivf_nearest_centroid(&vectors[i * D], out_centroids, D, k);
      if (nearest != assignments[i]) {
        assignments[i] = nearest;
        changed++;
      }
    }
    if (changed == 0)
      break;

    // Update step
    memset(new_centroids, 0, (size_t)k * D * sizeof(float));
    memset(counts, 0, k * sizeof(int));

    for (int i = 0; i < N; i++) {
      int c = assignments[i];
      counts[c]++;
      for (int d = 0; d < D; d++) {
        new_centroids[c * D + d] += vectors[i * D + d];
      }
    }

    for (int c = 0; c < k; c++) {
      if (counts[c] == 0) {
        // Empty cluster: reassign to farthest point from its nearest centroid
        float max_dist = -1.0f;
        int farthest = 0;
        for (int i = 0; i < N; i++) {
          float d = ivf_l2_dist(&vectors[i * D],
                                &out_centroids[assignments[i] * D], D);
          if (d > max_dist) {
            max_dist = d;
            farthest = i;
          }
        }
        memcpy(&out_centroids[c * D], &vectors[farthest * D],
               D * sizeof(float));
      } else {
        for (int d = 0; d < D; d++) {
          out_centroids[c * D + d] = new_centroids[c * D + d] / counts[c];
        }
      }
    }
  }

  sqlite3_free(assignments);
  sqlite3_free(new_centroids);
  sqlite3_free(counts);
  return 0;
}

#endif /* SQLITE_VEC_IVF_KMEANS_C */
