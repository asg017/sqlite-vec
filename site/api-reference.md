---
outline: 2
---

# API Reference

A complete reference to all the SQL scalar functions, table functions, and virtual tables inside `sqlite-vec`.

::: warning
sqlite-vec is pre-v1, so expect breaking changes.
:::

[[toc]]

## Constructors {#constructors} 

SQL functions that "construct" vectors with different element types.

Currently, only `float32`, `int8`, and `bit` vectors are supported.


### `vec_f32(vector)` {#vec_f32}

Creates a float vector from a BLOB or JSON text. If a BLOB is provided,
the length must be divisible by 4, as a float takes up 4 bytes of space each.

The returned value is a BLOB with 4 bytes per element, with a special [subtype](https://www.sqlite.org/c3ref/result_subtype.html)
of `223`.


```sql
select vec_f32('[.1, .2, .3, 4]');
-- X'CDCCCC3DCDCC4C3E9A99993E00008040'

select subtype(vec_f32('[.1, .2, .3, 4]'));
-- 223

select vec_f32(X'AABBCCDD');
-- X'AABBCCDD'

select vec_to_json(vec_f32(X'AABBCCDD'));
-- '[-1844071490169864000.000000]'

select vec_f32(X'AA');
-- ❌ invalid float32 vector BLOB length. Must be divisible by 4, found 1


```

### `vec_int8(vector)` {#vec_int8}

Creates a 8-bit integer vector from a BLOB or JSON text. If a BLOB is provided,
the length must be divisible by 4, as a float takes up 4 bytes of space each.
If JSON text is provided, each element must be an integer between -128 and 127 inclusive.

The returned value is a BLOB with 1 byte per element, with a special [subtype](https://www.sqlite.org/c3ref/result_subtype.html)
of `225`.


```sql
select vec_int8('[1, 2, 3, 4]');
-- X'01020304'

select subtype(vec_int8('[1, 2, 3, 4]'));
-- 225

select vec_int8(X'AABBCCDD');
-- X'AABBCCDD'

select vec_to_json(vec_int8(X'AABBCCDD'));
-- '[-86,-69,-52,-35]'

select vec_int8('[999]');
-- ❌ JSON parsing error: value out of range for int8


```

### `vec_bit(vector)` {#vec_bit}

Creates a binary vector from a BLOB.

The returned value is a BLOB with 1 byte per 8 elements, with a special [subtype](https://www.sqlite.org/c3ref/result_subtype.html)
of `224`.


```sql
select vec_bit(X'F0');
-- X'F0'

select subtype(vec_bit(X'F0'));
-- 224

select vec_to_json(vec_bit(X'F0'));
-- '[0,0,0,0,1,1,1,1]'


```

## Operations {#op} 

Different operations and utilities for working with vectors.


### `vec_length(vector)` {#vec_length}

Returns the number of elements in the given vector.
The vector can be `JSON`, `BLOB`, or the result of a [constructor function](#constructors).

This function will return an error if `vector` is invalid.


```sql
select vec_length('[.1, .2]');
-- 2

select vec_length(X'AABBCCDD');
-- 1

select vec_length(vec_int8(X'AABBCCDD'));
-- 4

select vec_length(vec_bit(X'AABBCCDD'));
-- 32

select vec_length(X'CCDD');
-- ❌ invalid float32 vector BLOB length. Must be divisible by 4, found 2


```

### `vec_type(vector)` {#vec_type}

Returns the name of the type of `vector` as text. One of `'float32'`, `'int8'`, or `'bit'`.

This function will return an error if `vector` is invalid.


```sql
select vec_type('[.1, .2]');
-- 'float32'

select vec_type(X'AABBCCDD');
-- 'float32'

select vec_type(vec_int8(X'AABBCCDD'));
-- 'int8'

select vec_type(vec_bit(X'AABBCCDD'));
-- 'bit'

select vec_type(X'CCDD');
-- ❌ invalid float32 vector BLOB length. Must be divisible by 4, found 2


```

### `vec_add(a, b)` {#vec_add}

Adds every element in vector `a` with vector `b`, returning a new vector `c`. Both vectors
must be of the same type and same length. Only `float32` and `int8` vectors are supported.

An error is raised if either `a` or `b` are invalid, or if they are not the same type or same length.

See also [`vec_sub()`](#vec_sub).


```sql
select vec_add(
  '[.1, .2, .3]',
  '[.4, .5, .6]'
);
-- X'0000003F3333333F6766663F'

select vec_to_json(
  vec_add(
    '[.1, .2, .3]',
    '[.4, .5, .6]'
  )
);
-- '[0.500000,0.700000,0.900000]'

select vec_to_json(
  vec_add(
    vec_int8('[1, 2, 3]'),
    vec_int8('[4, 5, 6]')
  )
);
-- '[5,7,9]'

select vec_add('[.1]', vec_int8('[1]'));
-- ❌ Vector type mistmatch. First vector has type float32, while the second has type int8.

select vec_add(vec_bit(X'AA'), vec_bit(X'BB'));
-- ❌ Cannot add two bitvectors together.


```

### `vec_sub(a, b)` {#vec_sub}

Subtracts every element in vector `a` with vector `b`, returning a new vector `c`. Both vectors
must be of the same type and same length. Only `float32` and `int8` vectors are supported.

An error is raised if either `a` or `b` are invalid, or if they are not the same type or same length.

See also [`vec_add()`](#vec_add).


```sql
select vec_sub(
  '[.1, .2, .3]',
  '[.4, .5, .6]'
);
-- X'9A9999BE9A9999BE9A9999BE'

select vec_to_json(
  vec_sub(
    '[.1, .2, .3]',
    '[.4, .5, .6]'
  )
);
-- '[-0.300000,-0.300000,-0.300000]'

select vec_to_json(
  vec_sub(
    vec_int8('[1, 2, 3]'),
    vec_int8('[4, 5, 6]')
  )
);
-- '[-3,-3,-3]'

select vec_sub('[.1]', vec_int8('[1]'));
-- ❌ Vector type mistmatch. First vector has type float32, while the second has type int8.

select vec_sub(vec_bit(X'AA'), vec_bit(X'BB'));
-- ❌ Cannot subtract two bitvectors together.


```

### `vec_normalize(vector)` {#vec_normalize}

Performs L2 normalization on the given vector. Only float32 vectors are currently supported.

Returns an error if the input is an invalid vector or not a float32 vector.


```sql
select vec_normalize('[2, 3, 1, -4]');
-- X'BAF4BA3E8B370C3FBAF43A3EBAF43ABF'

select vec_to_json(
  vec_normalize('[2, 3, 1, -4]')
);
-- '[0.365148,0.547723,0.182574,-0.730297]'

-- for matryoshka embeddings - slice then normalize
select vec_to_json(
  vec_normalize(
    vec_slice('[2, 3, 1, -4]', 0, 2)
  )
);
-- '[0.554700,0.832050]'


```

### `vec_slice(vector, start, end)` {#vec_slice}

Extract a subset of `vector` from the `start` element (inclusive) to the `end` element (exclusive). TODO check

This is especially useful for [Matryoshka embeddings](#TODO), also known as "adaptive length" embeddings.
Use with [`vec_normalize()`](#vec_normalize) to get proper results.

Returns an error in the following conditions:
  - If `vector` is not a valid vector
  - If `start` is less than zero or greater than or equal to `end`
  - If `end` is greater than the length of `vector`, or less than or equal to `start`.
  - If `vector` is a bitvector, `start` and `end` must be divisible by 8.


```sql
select vec_slice('[1, 2,3, 4]', 0, 2);
-- X'0000803F00000040'

select vec_to_json(
  vec_slice('[1, 2,3, 4]', 0, 2)
);
-- '[1.000000,2.000000]'

select vec_to_json(
  vec_slice('[1, 2,3, 4]', 2, 4)
);
-- '[3.000000,4.000000]'

select vec_to_json(
  vec_slice('[1, 2,3, 4]', -1, 4)
);
-- ❌ slice 'start' index must be a postive number.

select vec_to_json(
  vec_slice('[1, 2,3, 4]', 0, 5)
);
-- ❌ slice 'end' index is greater than the number of dimensions

select vec_to_json(
  vec_slice('[1, 2,3, 4]', 0, 0)
);
-- ❌ slice 'start' index is equal to the 'end' index, vectors must have non-zero length


```

### `vec_to_json(vector)` {#vec_to_json}

Represents a vector as JSON text. The input vector can be a vector BLOB or JSON text.

Returns an error if `vector` is an invalid vector, or when memory cannot be allocated.


```sql
select vec_to_json(X'AABBCCDD');
-- '[-1844071490169864000.000000]'

select vec_to_json(vec_int8(X'AABBCCDD'));
-- '[-86,-69,-52,-35]'

select vec_to_json(vec_bit(X'AABBCCDD'));
-- '[0,1,0,1,0,1,0,1,1,1,0,1,1,1,0,1,0,0,1,1,0,0,1,1,1,0,1,1,1,0,1,1]'

select vec_to_json('[1,2,3,4]');
-- '[1.000000,2.000000,3.000000,4.000000]'

select vec_to_json('invalid');
-- ❌ JSON array parsing error: Input does not start with '['


```

### `vec_each(vector)` {#vec_each}

A table function to iterate through every element in a vector. One row id returned per element in a vector.

```sql
CREATE TABLE vec_each(
  rowid int,    -- The
  vector HIDDEN -- input parameter: A well-formed vector value
)
```

Returns an error if `vector` is not a valid vector.


```sql
select rowid, value from vec_each('[1,2,3,4]');
/*
┌───────┬───────┐
│ rowid │ value │
├───────┼───────┤
│ 0     │ 1     │
├───────┼───────┤
│ 1     │ 2     │
├───────┼───────┤
│ 2     │ 3     │
├───────┼───────┤
│ 3     │ 4     │
└───────┴───────┘

*/


select rowid, value from vec_each(X'AABBCCDD00112233');
/*
┌───────┬──────────────────────┐
│ rowid │ value                │
├───────┼──────────────────────┤
│ 0     │ -1844071490169864200 │
├───────┼──────────────────────┤
│ 1     │ 3.773402568185702e-8 │
└───────┴──────────────────────┘

*/


select rowid, value from vec_each(vec_int8(X'AABBCCDD'));
/*
┌───────┬───────┐
│ rowid │ value │
├───────┼───────┤
│ 0     │ -86   │
├───────┼───────┤
│ 1     │ -69   │
├───────┼───────┤
│ 2     │ -52   │
├───────┼───────┤
│ 3     │ -35   │
└───────┴───────┘

*/


select rowid, value from vec_each(vec_bit(X'F0'));
/*
┌───────┬───────┐
│ rowid │ value │
├───────┼───────┤
│ 0     │ 1     │
├───────┼───────┤
│ 1     │ 1     │
├───────┼───────┤
│ 2     │ 1     │
├───────┼───────┤
│ 3     │ 1     │
├───────┼───────┤
│ 4     │ 0     │
├───────┼───────┤
│ 5     │ 0     │
├───────┼───────┤
│ 6     │ 0     │
├───────┼───────┤
│ 7     │ 0     │
└───────┴───────┘

*/



```

## Distance functions {#distance} 

Various algorithms to calculate distance between two vectors.

### `vec_distance_L2(a, b)` {#vec_distance_L2}

Calculates the L2 euclidian distance between vectors `a` and `b`. Only valid for float32 or int8 vectors.

Returns an error under the following conditions:
- `a` or `b` are invalid vectors
- `a` or `b` do not share the same vector element types (ex float32 or int8)
- `a` or `b` are bit vectors. Use [`vec_distance_hamming()`](#vec_distance_hamming) for distance calculations between two bitvectors.
- `a` or `b` do not have the same length.


```sql
select vec_distance_L2('[1, 1]', '[2, 2]');
-- 1.4142135381698608

select vec_distance_L2('[1, 1]', '[-2, -2]');
-- 4.242640495300293

select vec_distance_L2('[1.1, 2.2, 3.3]', '[4.4, 5.5, 6.6]');
-- 5.7157673835754395

select vec_distance_L2(X'AABBCCDD', X'00112233');
-- 1844071490169864200

select vec_distance_L2('[1, 1]', vec_int8('[2, 2]'));
-- ❌ Vector type mistmatch. First vector has type float32, while the second has type int8.

select vec_distance_L2(vec_bit(X'AA'), vec_bit(X'BB'));
-- ❌ Cannot calculate L2 distance between two bitvectors.


```

### `vec_distance_cosine(a, b)` {#vec_distance_cosine}

Calculates the cosine distance between vectors `a` and `b`. Only valid for float32 or int8 vectors.

Returns an error under the following conditions:
  - `a` or `b` are invalid vectors
  - `a` or `b` do not share the same vector element types (ex float32 or int8)
  - `a` or `b` are bit vectors. Use [`vec_distance_hamming()`](#vec_distance_hamming) for distance calculations between two bitvectors.
  - `a` or `b` do not have the same length.


```sql
select vec_distance_cosine('[1, 1]', '[2, 2]');
-- 2.220446049250313e-16

select vec_distance_cosine('[1, 1]', '[-2, -2]');
-- 2

select vec_distance_cosine('[1.1, 2.2, 3.3]', '[4.4, 5.5, 6.6]');
-- 0.02536807395517826

select vec_distance_cosine(X'AABBCCDD', X'00112233');
-- 2

select vec_distance_cosine('[1, 1]', vec_int8('[2, 2]'));
-- ❌ Vector type mistmatch. First vector has type float32, while the second has type int8.

select vec_distance_cosine(vec_bit(X'AA'), vec_bit(X'BB'));
-- ❌ Cannot calculate cosine distance between two bitvectors.


```

### `vec_distance_hamming(a, b)` {#vec_distance_hamming}

Calculates the hamming distance between two bitvectors `a` and `b`. Only valid for bitvectors.

Returns an error under the following conditions:
- `a` or `b` are not bitvectors
- `a` and `b` do not share the same length
- Memory cannot be allocated


```sql
select vec_distance_hamming(vec_bit(X'00'), vec_bit(X'FF'));
-- 8

select vec_distance_hamming(vec_bit(X'FF'), vec_bit(X'FF'));
-- 0

select vec_distance_hamming(vec_bit(X'F0'), vec_bit(X'44'));
-- 4

select vec_distance_hamming('[1, 1]', '[0, 0]');
-- ❌ Cannot calculate hamming distance between two float32 vectors.


```

## Quantization {#quantization} 

Various techniques to "compress" a vector by reducing precision and accuracy.

### `vec_quantize_binary(vector)` {#vec_quantize_binary}

Quantize a float32 or int8 vector into a bitvector.
For every element in the vector, a `1` is assigned to positive numbers and a `0` is assigned to negative numbers.
These values are then packed into a bit vector.

Returns an error if `vector` is invalid, or if `vector` is not a float32 or int8 vector.


```sql
select vec_quantize_binary('[1, 2, 3, 4, 5, 6, 7, 8]');
-- X'FF'

select vec_quantize_binary('[1, 2, 3, 4, -5, -6, -7, -8]');
-- X'0F'

select vec_quantize_binary('[-1, -2, -3, -4, -5, -6, -7, -8]');
-- X'00'

select vec_quantize_binary('[-1, -2, -3, -4, -5, -6, -7, -8]');
-- X'00'

select vec_quantize_binary(vec_int8(X'11223344'));
-- ❌ Binary quantization requires vectors with a length divisible by 8

select vec_quantize_binary(vec_bit(X'FF'));
-- ❌ Can only binary quantize float or int8 vectors


```

### `vec_quantize_i8(vector, [start], [end])` {#vec_quantize_i8}

x

```sql
select 'todo';
-- 'todo'


```

## NumPy Utilities {#numpy} 

Functions to read data from or work with [NumPy arrays](https://numpy.org/doc/stable/reference/generated/numpy.array.html).

### `vec_npy_each(vector)` {#vec_npy_each}

xxx


```sql
-- db.execute('select quote(?)', [to_npy(np.array([[1.0], [2.0], [3.0]], dtype=np.float32))]).fetchone()
select
  rowid,
  vector,
  vec_type(vector),
  vec_to_json(vector)
from vec_npy_each(
  X'934E554D5059010076007B276465736372273A20273C6634272C2027666F727472616E5F6F72646572273A2046616C73652C20277368617065273A2028332C2031292C207D202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020200A0000803F0000004000004040'
)
/*
┌───────┬─────────────┬──────────────────┬─────────────────────┐
│ rowid │ vector      │ vec_type(vector) │ vec_to_json(vector) │
├───────┼─────────────┼──────────────────┼─────────────────────┤
│ 0     │ X'0000803F' │ 'float32'        │ '[1.000000]'        │
├───────┼─────────────┼──────────────────┼─────────────────────┤
│ 1     │ X'00000040' │ 'float32'        │ '[2.000000]'        │
├───────┼─────────────┼──────────────────┼─────────────────────┤
│ 2     │ X'00004040' │ 'float32'        │ '[3.000000]'        │
└───────┴─────────────┴──────────────────┴─────────────────────┘

*/


-- db.execute('select quote(?)', [to_npy(np.array([[1.0], [2.0], [3.0]], dtype=np.float32))]).fetchone()
select
  rowid,
  vector,
  vec_type(vector),
  vec_to_json(vector)
from vec_npy_each(
  X'934E554D5059010076007B276465736372273A20273C6634272C2027666F727472616E5F6F72646572273A2046616C73652C20277368617065273A2028332C2031292C207D202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020200A0000803F0000004000004040'
)
/*
┌───────┬─────────────┬──────────────────┬─────────────────────┐
│ rowid │ vector      │ vec_type(vector) │ vec_to_json(vector) │
├───────┼─────────────┼──────────────────┼─────────────────────┤
│ 0     │ X'0000803F' │ 'float32'        │ '[1.000000]'        │
├───────┼─────────────┼──────────────────┼─────────────────────┤
│ 1     │ X'00000040' │ 'float32'        │ '[2.000000]'        │
├───────┼─────────────┼──────────────────┼─────────────────────┤
│ 2     │ X'00004040' │ 'float32'        │ '[3.000000]'        │
└───────┴─────────────┴──────────────────┴─────────────────────┘

*/



```

## Meta {#meta} 

Helper functions to debug `sqlite-vec` installations.

### `vec_version()` {#vec_version}

Returns a version string of the current `sqlite-vec` installation.

```sql
select vec_version();
-- 'v0.0.1-alpha.37'


```

### `vec_debug()` {#vec_debug}

Returns debugging information of the current `sqlite-vec` installation.

```sql
select vec_debug();
/*
'Version: v0.0.1-alpha.37
Date: 2024-07-23T14:09:43Z-0700
Commit: 77f9b0374c8129056b344854de2dff6b103e5729
Build flags: avx '
*/


```

## Entrypoints {#entrypoints} 

All the named entrypoints that load in different `sqlite-vec` functions and options.

