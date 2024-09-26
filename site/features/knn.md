# KNN queries

The most common use-case for vectors in databases is for K-nearest-neighbors (KNN) queries.
You'll have a table of vectors, and you'll want to find the K closest

Currently there are two ways to to perform KNN queries with `sqlite-vec`:
With `vec0` virtual tables and "manually" with regular tables.

The `vec0` virtual table is faster and more compact, but is less flexible and requires `JOIN`s back to your source tables.
The "manual" method is more flexible and



## `vec0` virtual tables

```sql
create virtual table vec_documents using vec0(
  document_id integer primary key,
  contents_embedding float[768]
);

insert into vec_documents(document_id, contents_embedding)
  select id, embed(contents)
  from documents;
```


```sql
select
  document_id,
  distance
from vec_documents
where contents_embedding match :query
  and k = 10;
```

```sql
-- This example ONLY works in SQLite versions 3.41+
-- Otherwise, use the `k = 10` method described above!
select
  document_id,
  distance
from vec_documents
where contents_embedding match :query
limit 10; -- LIMIT only works on SQLite versions 3.41+
```

```sql
with knn_matches as (
  select
    document_id,
    distance
  from vec_documents
  where contents_embedding match :query
    and k = 10
)
select
  documents.id,
  documents.contents,
  knn_matches.distance
from knn_matches
left join documents on documents.id = knn_matches.document_id
```


```sql
create virtual table vec_documents using vec0(
  document_id integer primary key,
  contents_embedding float[768] distance_metric=cosine
);

-- insert vectors into vec_documents...


-- this MATCH will now use cosine distance instead of the default L2 distance
select
  document_id,
  distance
from vec_documents
where contents_embedding match :query
  and k = 10;
```


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
