# API Reference

::: warning
sqlite-vec is pre-v1, so expect breaking changes.
:::

[[toc]]

## Meta {#meta} 

TODO

### `vec_version()` {#vec_version}

Returns a version string of the current `sqlite-vec` installation.

```sql
select vec_version();
-- 'v0.0.1-alpha.33'


```

### `vec_debug()` {#vec_debug}

Returns debugging information of the current `sqlite-vec` installation.

```sql
select vec_debug();
/*
'Version: v0.0.1-alpha.33
Date: 2024-07-14T14:24:27Z-0700
Commit: 18e33edf143cafd881643965a559cd0259ab0666
Build flags: avx '
*/


```

## Constructors {#constructors} 

TODO

### `vec_f32(vector)` {#vec_f32}

Creates a float vector from a BLOB or JSON text. If a BLOB is provided,
the length must be divisible by 4, as a float takes up 4 bytes of space each.

The returned value is a BLOB with 4 bytes per element, with a special [subtype](https://www.sqlite.org/c3ref/result_subtype.html)
of `223`.


```sql
select vec_f32('[.1, .2, .3, 4]');
-- X'CDCCCC3DCDCC4C3E9A99993E008040'

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
-- X'1234'

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

The returned value is a BLOB with 4 bytes per element, with a special [subtype](https://www.sqlite.org/c3ref/result_subtype.html)
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

TODO

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

### `vec_add(a, b)` {#vec_add}

Adds every element in vector `a` with vector `b`, returning a new vector `c`. Both vectors
must be of the same type and same length. Only `float32` and `int8` vectors are supported.

An error is raised if either `a` or `b` are invalid, or if they are not the same type or same length.


```sql
select vec_add(
  '[.1, .2, .3]',
  '[.4, .5, .6]'
);
-- X'0003F3333333F6766663F'

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

x

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

x

```sql
select 'todo';
-- 'todo'


```

### `vec_slice(vector, start, end)` {#vec_slice}

x

```sql
select 'todo';
-- 'todo'


```

### `vec_to_json(vector)` {#vec_to_json}

x

```sql
select 'todo';
-- 'todo'


```

## Distance functions {#distance} 

TODO

### `vec_distance_cosine(a, b)` {#vec_distance_cosine}

x

```sql
select 'todo';
-- 'todo'


```

### `vec_distance_hamming(a, b)` {#vec_distance_hamming}

x

```sql
select 'todo';
-- 'todo'


```

### `vec_distance_l2(a, b)` {#vec_distance_l2}

x

```sql
select 'todo';
-- 'todo'


```

## Quantization {#quantization} 

TODO

### `vec_quantize_binary(vector)` {#vec_quantize_binary}

x

```sql
select 'todo';
-- 'todo'


```

### `vec_quantize_i8(vector, [start], [end])` {#vec_quantize_i8}

x

```sql
select 'todo';
-- 'todo'


```

