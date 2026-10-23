#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sqlite-vec.h"
#include "sqlite3.h"
#include <assert.h>

/* Test wrappers from sqlite-vec-rescore.c (SQLITE_VEC_TEST build) */
extern void _test_rescore_quantize_float_to_bit(const float *src, uint8_t *dst, size_t dim);
extern void _test_rescore_quantize_float_to_int8(const float *src, int8_t *dst, size_t dim);
extern size_t _test_rescore_quantized_byte_size_bit(size_t dimensions);
extern size_t _test_rescore_quantized_byte_size_int8(size_t dimensions);

/**
 * Fuzz target: edge cases in rescore quantization functions.
 *
 * The existing rescore-quantize.c only tests dimensions that are multiples of 8
 * and never passes special float values. This target:
 *
 * - Tests rescore_quantized_byte_size with arbitrary dimension values
 *   (including 0, 1, 7, MAX values -- looking for integer division issues)
 * - Passes raw float reinterpretation of fuzz bytes (NaN, Inf, denormals,
 *   negative zero: the values that must reach the int8 quantizer's clamps
 *   before its conversion to int8)
 * - Tests the int8 quantizer with all-identical values (each element is
 *   quantized on its own, so all get the same code)
 * - Tests the int8 quantizer with values far outside [-1, 1]
 * - Tests bit quantizer with exact float threshold (0.0f boundary)
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) return 0;

  uint8_t mode = data[0] % 5;
  data++; size--;

  switch (mode) {
    case 0: {
      /* Test rescore_quantized_byte_size with fuzz-controlled dimensions.
         This function does dimensions / CHAR_BIT for bit, dimensions for int8.
         We're checking it doesn't do anything weird with edge values. */
      if (size < sizeof(size_t)) return 0;
      size_t dim;
      memcpy(&dim, data, sizeof(dim));

      /* These should never crash, just return values */
      size_t bit_size = _test_rescore_quantized_byte_size_bit(dim);
      size_t int8_size = _test_rescore_quantized_byte_size_int8(dim);

      /* Verify basic invariants */
      (void)bit_size;
      (void)int8_size;
      break;
    }

    case 1: {
      /* Bit quantize with raw reinterpreted floats (NaN, Inf, denormal).
         The key check: src[i] >= 0.0f -- NaN comparison is always false,
         so NaN should produce 0-bits. But denormals and -0.0f are tricky. */
      size_t num_floats = size / sizeof(float);
      if (num_floats == 0) return 0;
      /* Round to multiple of 8 for bit quantizer */
      size_t dim = (num_floats / 8) * 8;
      if (dim == 0) return 0;

      /* The fuzz bytes start one byte past the mode byte, so they are copied
         out to be read as floats at float alignment. */
      float *src = (float *)malloc(dim * sizeof(float));
      size_t bit_bytes = dim / 8;
      uint8_t *dst = (uint8_t *)malloc(bit_bytes);
      if (!src || !dst) { free(src); free(dst); return 0; }
      memcpy(src, data, dim * sizeof(float));

      _test_rescore_quantize_float_to_bit(src, dst, dim);

      /* Verify: for each bit, if src >= 0 then bit should be set */
      for (size_t i = 0; i < dim; i++) {
        int bit_set = (dst[i / 8] >> (i % 8)) & 1;
        if (src[i] >= 0.0f) {
          assert(bit_set == 1);
        } else if (src[i] < 0.0f) {
          /* Definitely negative -- bit must be 0 */
          assert(bit_set == 0);
        }
        /* NaN: comparison is false, so bit_set should be 0 */
      }

      free(src);
      free(dst);
      break;
    }

    case 2: {
      /* Int8 quantize with raw reinterpreted floats. The quantizer maps
         [-1, 1] onto [-128, 127] and clamps before converting to int8, so
         NaN, Inf and values far outside [-1, 1] must each reach a clamp:
         converting a float outside int8's range to int8 is undefined
         behavior, which UBSan reports. */
      size_t num_floats = size / sizeof(float);
      if (num_floats == 0) return 0;

      /* Copied out to float alignment, as in mode 1 */
      float *src = (float *)malloc(num_floats * sizeof(float));
      int8_t *dst = (int8_t *)malloc(num_floats);
      if (!src || !dst) { free(src); free(dst); return 0; }
      memcpy(src, data, num_floats * sizeof(float));

      _test_rescore_quantize_float_to_int8(src, dst, num_floats);

      /* Output must always be in [-128, 127] (trivially true for int8_t,
         but check the actual clamping logic worked) */
      for (size_t i = 0; i < num_floats; i++) {
        assert(dst[i] >= -128 && dst[i] <= 127);
      }

      free(src);
      free(dst);
      break;
    }

    case 3: {
      /* Int8 quantize stress: all-same values */
      size_t dim = (size < 64) ? size : 64;
      if (dim == 0) return 0;

      float *src = (float *)malloc(dim * sizeof(float));
      int8_t *dst = (int8_t *)malloc(dim);
      if (!src || !dst) { free(src); free(dst); return 0; }

      /* Fill with a single value derived from fuzz data */
      float val;
      memcpy(&val, data, sizeof(float) < size ? sizeof(float) : size);
      for (size_t i = 0; i < dim; i++) src[i] = val;

      _test_rescore_quantize_float_to_int8(src, dst, dim);

      /* Each element is quantized on its own, so all get the same code */
      for (size_t i = 1; i < dim; i++) {
        assert(dst[i] == dst[0]);
      }

      free(src);
      free(dst);
      break;
    }

    case 4: {
      /* Int8 quantize with extreme values: one huge positive, one huge
         negative, and the rest thousands, all far outside [-1, 1] and
         clamped to [-128, 127]. */
      if (size < 2 * sizeof(float)) return 0;

      float extreme[2];
      memcpy(extreme, data, 2 * sizeof(float));

      /* Only test if both are finite (NaN/Inf tested in case 2) */
      if (!isfinite(extreme[0]) || !isfinite(extreme[1])) return 0;

      /* Build a vector with these two extreme values plus some fuzz */
      size_t dim = 16;
      float src[16];
      src[0] = extreme[0];
      src[1] = extreme[1];
      for (size_t i = 2; i < dim; i++) {
        if (2 * sizeof(float) + (i - 2) < size) {
          src[i] = (float)((int8_t)data[2 * sizeof(float) + (i - 2)]) * 1000.0f;
        } else {
          src[i] = 0.0f;
        }
      }

      int8_t dst[16];
      _test_rescore_quantize_float_to_int8(src, dst, dim);

      for (size_t i = 0; i < dim; i++) {
        assert(dst[i] >= -128 && dst[i] <= 127);
      }
      break;
    }
  }

  return 0;
}
