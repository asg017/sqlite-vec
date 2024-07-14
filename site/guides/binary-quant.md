# Binary Quantization

"Quantization" refers to a variety of methods and techniques for reducing the
size of vectors in a vector index. **Binary quantization** (BQ) refers to a
specific technique where each individual floating point element in a vector is
reduced to a single bit, typically by assigning `0` to negative numbers and `1`
to positive numbers.

For example, in this 8-dimensional `float32` vector:

```json
[-0.73, -0.80, 0.12, -0.73, 0.79, -0.11, 0.23, 0.97]
```

Applying binary quantization would result in the following `bit` vector:

```json
[0, 0, 1, 0, 1, 0, 1, 1]
```

The original 8-dimensional `float32` vector requires `8 * 4 = 32` bytes of space
to store. For 1 million vectors, that would be `32MB`. On the other hand, the
binary quantized 8-dimensional vector can be stored in a single byte — one bit
per element. For 1 million vectors, that would be just `1MB`, a 32x reduction!

Though keep in mind, you're bound to lose a lot quality when reducing 32 bits of
information to 1 bit. [Oversampling and re-scoring](#re-scoring) will help a
lot.

The main goal of BQ is to dramatically reduce the size of your vector index,
resulting in faster searches with less resources. This is especially useful in
`sqlite-vec`, which is (currently) brute-force only and meant to run on small
devices. BQ is an easy low-cost method to make larger vector datasets easier to
manage.

## Binary Quantization `sqlite-vec`

The `sqlite-vec` extension offers a `vec_quantize_binary()` SQL scalar function,
which applies binary quanitization to a `float32` or `int8` vector. For every
element in a given vector, it will apply `0` to negative values and `1` to
positive values, and pack them into a `BLOB`.

```sqlite
select vec_quantize_binary(
  '[-0.73, -0.80, 0.12, -0.73, 0.79, -0.11, 0.23, 0.97]'
);
-- X'd4`
```

The single byte `0xd4` in hexadecimal is `11010100` in binary.

<!-- TODO what https://github.com/asg017/sqlite-vec/issues/23 -->

## Demo

Here's an end-to-end example of using binary quantization with `vec0` virtual
tables in `sqlite-vec`.

```sqlite
create virtual table vec_movies using vec0(
  synopsis_embedding bit[768]
);
```

```sqlite
insert into vec_movies(rowid, synopsis_embedding)
 VALUES (:id, vec_quantize_binary(:vector));
```

```sqlite
select
  rowid,
  distance
from vec_movies
where synopsis_embedding match vec_quantize_binary(:query)
order by distance
limit 20;
```

### Re-scoring

```sqlite
create virtual table vec_movies using vec0(
  synopsis_embedding float[768],
  synopsis_embedding_coarse bit[768]
);
```

```sqlite
insert into vec_movies(rowid, synopsis_embedding, synopsis_embedding_coarse)
 VALUES (:id, :vector, vec_quantize_binary(:vector));
```

```sqlite
with coarse_matches as (
  select
    rowid,
    synopsis_embedding
  from vec_movies
  where synopsis_embedding_coarse match vec_quantize_binary(:query)
  order by distance
  limit 20 * 8
),
select
  rowid,
  vec_distance_L2(synopsis_embedding, :query)
from coarse_matches
order by 2
limit 20;
```

# Benchmarks

## Model support

Certain embedding models, like [Nomic](https://nomic.ai/)'s
[`nomic-embed-text-v1.5`](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5)
text embedding model and
[mixedbread.ai](https://www.mixedbread.ai/blog/mxbai-embed-2d-large-v1)'s
[`mxbai-embed-large-v1`](https://huggingface.co/mixedbread-ai/mxbai-embed-large-v1)
are specifically trained to perform well after binary quantization.

Other embeddings models may not, but you can still try BQ and see if it works
for your datasets. Chances are, if your vectors are normalized (ie between
`-1.0` and `1.0`) there's a good chance you will see acceptable results with BQ.
