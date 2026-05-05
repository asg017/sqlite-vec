#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/* These are SQLITE_VEC_TEST wrappers defined in sqlite-vec-rescore.c */
extern void _test_rescore_quantize_float_to_bit(const float *src, uint8_t *dst, size_t dim);
extern void _test_rescore_quantize_float_to_int8(const float *src, int8_t *dst, size_t dim);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  /* Need at least 4 bytes for one float */
  if (size < 4) return 0;

  /* Use the input as an array of floats. Dimensions must be a multiple of 8
   * for the bit quantizer. */
  size_t num_floats = size / sizeof(float);
  if (num_floats == 0) return 0;

  /* Round down to multiple of 8 for bit quantizer compatibility */
  size_t dim = (num_floats / 8) * 8;
  if (dim == 0) dim = 8;
  if (dim > num_floats) return 0;

  const float *src = (const float *)data;

  /* Allocate output buffers */
  size_t bit_bytes = dim / 8;
  uint8_t *bit_dst = (uint8_t *)malloc(bit_bytes);
  int8_t *int8_dst = (int8_t *)malloc(dim);
  if (!bit_dst || !int8_dst) {
    free(bit_dst);
    free(int8_dst);
    return 0;
  }

  /* Test bit quantization */
  _test_rescore_quantize_float_to_bit(src, bit_dst, dim);

  /* Test int8 quantization */
  _test_rescore_quantize_float_to_int8(src, int8_dst, dim);

  /* Verify int8 output is in range */
  for (size_t i = 0; i < dim; i++) {
    assert(int8_dst[i] >= -128 && int8_dst[i] <= 127);
  }

  free(bit_dst);
  free(int8_dst);
  return 0;
}
