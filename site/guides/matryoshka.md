# Matryoshka (Adaptive-Length) Embeddings

Matryoshka embeddings are a new class of embedding models introduced in the
TODO-YYY paper [_TODO title_](https://arxiv.org/abs/2205.13147). They allow one
to truncate excess dimensions in large vector, without lossing much quality.

Let's say your embedding model generate 1024-dimensional vectors. If you have 1
million of these 1024-dimensional vectors, they would take up `4.096 GB` of
space! You're not able to reduce the dimensions without lossing a lot of
quality - if you were to remove half of the dimensions 512-dimensional vectors,
you could expect to also lose 50% or more of the quality of results. There are
other dimensional-reduction techniques, like [PCA](#TODO), but this requires a
complicated and expensive training process.

Matryoshka embeddings, on the other hand, _can_ be truncated, without losing
quality. Using [`mixedbread.ai`](#TODO) `mxbai-embed-large-v1` model, they claim
that

They are called "Matryoshka" embeddings because ... TODO

## Matryoshka Embeddings with `sqlite-vec`

You can use a combination of [`vec_slice()`](/api-reference#vec_slice) and
[`vec_normalize()`](/api-reference#vec_slice) on Matryoshka embeddings to
truncate.

```sql
select
  vec_normalize(vec_slice(title_embeddings, 0, 256)) as title_embeddings_256d
from vec_articles;
```

## Benchmarks

## Suppported Models

https://supabase.com/blog/matryoshka-embeddings#which-granularities-were-openais-text-embedding-3-models-trained-on

`text-embedding-3-small`: 1536, 512 `text-embedding-3-large`: 3072, 1024, 256

https://x.com/ZainHasan6/status/1757519325202686255

`text-embeddings-3-large:` 3072, 1536, 1024, 512

https://www.mixedbread.ai/blog/binary-mrl

`mxbai-embed-large-v1`: 1024, 512, 256, 128, 64

`nomic-embed-text-v1.5`: 768, 512, 256, 128, 64
