#include <stdlib.h>

int min_idx(
  // list of distances, size n
  const float *distances,
  // number of entries in distances
  int32_t n,
  // output array of size k, the indicies of the lowest k values in distances
  int32_t *out,
  // output number of elements
  int32_t k
);
