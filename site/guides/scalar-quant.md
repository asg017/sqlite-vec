# Scalar Quantization (SQ)

"Quantization" refers to a variety of methods and techniques for reducing the
size of vectors in a vector index. **Scalar quantization** (SQ) refers to a
specific technique where each individual floating point element in a vector is
scaled to a small element type, like `float16`, `int8`.

Most embedding models generate `float32` vectors. Each `float32` takes up 4
bytes of space. This can add up, especially when working with a large amount of
vectors or vectors with many dimensions. However, if you scale them to `float16`
or `int8` vectors, they only take up 2 bytes of space and 1 bytes of space
respectively, saving you precious space at the expense of some quality.

```sql
select vec_quantize_float16(vec_f32('[]'), 'unit');
select vec_quantize_int8(vec_f32('[]'), 'unit');

select vec_quantize('float16', vec_f32('...'));
select vec_quantize('int8', vec_f32('...'));
select vec_quantize('bit', vec_f32('...'));

select vec_quantize('sqf16', vec_f32('...'));
select vec_quantize('sqi8', vec_f32('...'));
select vec_quantize('bq2', vec_f32('...'));
```

## Benchmarks
