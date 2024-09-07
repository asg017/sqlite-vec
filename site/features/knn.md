# KNN queries

## `vec0` virtual tables

<!-- TODO match on vector column, k vs limit, distance_metric configurable, etc.-->

## Manually with SQL scalar functions

You don't need a `vec0` virtual table to perform KNN searches with `sqlite-vec`.
You could store vectors in regular columns in a regular tables, like so:

```sql
create table documents(
  id integer primary key,
  contents text,
  -- a 4-dimensional floating-point vector
  contents_embedding blob
);

insert into documents values
  (1, 'alex', vec_f32('[1.1, 1.1, 1.1, 1.1]')),
  (2, 'brian', vec_f32('[2.2, 2.2, 2.2, 2.2]')),
  (3, 'craig', vec_f32('[3.3, 3.3, 3.3, 3.3]'));
```

When you want to find similar vectors, you can manually use
[`vec_distance_L2()`](../api-reference.md#vec_distance_l2),
[`vec_distance_L1()`](../api-reference.md#vec_distance_l1),
or [`vec_distance_cosine()`](../api-reference.md#vec_distance_cosine),
and an `ORDER BY` clause to perform a brute-force KNN query.

```sql
select
  id,
  contents,
  vec_distance_L2(contents_embedding, '[2.2, 2.2, 2.2, 2.2]') as distance
from documents
order by distance;

/*
┌────┬──────────┬──────────────────┐
│ id │ contents │     distance     │
├────┼──────────┼──────────────────┤
│ 2  │ 'brian'  │ 0.0              │
│ 3  │ 'craig'  │ 2.19999980926514 │
│ 1  │ 'alex'   │ 2.20000004768372 │
└────┴──────────┴──────────────────┘
*/
```




If you choose this approach, it is recommended to define the "vector column" with its element type (`float`, `bit`, etc.) and dimension, for better documentation.
It's also recommended to include a
[`CHECK` constraint](https://www.sqlite.org/lang_createtable.html#check_constraints),
to ensure only vectors of the correct element type and dimension exist in the table.

```sql
create table documents(
  id integer primary key,
  contents text,
  contents_embedding float[4]
    check(
      typeof(contents_embedding) == 'blob'
      and vec_length(contents_embedding) == 4
    )
);

-- ❌ Fails, needs to be a BLOB input
insert into documents values (1, 'alex', '[1.1, 1.1, 1.1, 1.1]');

-- ❌ Fails, 3 dimensions, needs 4
insert into documents values (1, 'alex', vec_f32('[1.1, 1.1, 1.1]'));

-- ❌ Fails, needs to be a float32 vector
insert into documents values (1, 'alex', vec_bit('[1.1, 1.1, 1.1, 1.1]'));

-- ✅ Success! 
insert into documents values (1, 'alex', vec_f32('[1.1, 1.1, 1.1, 1.1]'));
```

Keep in mind: **SQLite does not support custom types.**
The example above may look like that the `contents_embedding` column has a "custom type"
of `float[4]`, but SQLite allows for *anything* to appear as a "column type".

```sql
-- these "column types" are totally legal in SQLite
create table students(
  name ham_sandwich,
  age minions[42]
);
```

See [Datatypes in SQLite](https://www.sqlite.org/datatype3.html) for more info.

So by itself, `float[4]` as a "column type" is not enforced by SQLite at all.
This is why we recommend including `CHECK` constraints, to enforce that values in your vector column
are of the correct type and length.

For [strict tables](https://www.sqlite.org/stricttables.html), use the `BLOB` type and include the same `CHECK` constraints.

```sql

create table documents(
  id integer primary key,
  contents text,
  contents_embedding blob check(vec_length(contents_embedding) == 4)
) strict;
```

<!--
TODO:

- performance (brute force, vec0 is faster bc chunking, larger rows, move to separate table, etc.)
- configurable "distance metrics"
- note on `bit[]` and `int[8]` columns, require the constructor functions

-->

<!--## Static Blobs-->
