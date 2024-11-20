# Experimental Metadata Filtering Builds

The `sqlite-vec` project has a series of pull requests
([#122](https://github.com/asg017/sqlite-vec/pull/122),
[#123](https://github.com/asg017/sqlite-vec/pull/123), and
[#124](https://github.com/asg017/sqlite-vec/pull/124)) that will add proper
metadata column support to `vec0` virtual tables.

But they aren't merged yet! So I've packaged pre-compiled extensions with these
features baked in, so that others can try it for themselves. Once those pull
requests are merged, this page will be removed.

As a quick sample, this is what metadata columns will look like:

```sql
create virtual table vec_movies using vec0(
  -- aliased primary key
  movie_id integer primary key,

  -- vector column
  synopsis_embedding float[1024],

  -- partition key (internally shards vectors)
  user_id integer primary key,

  -- metadata columns (indexed alongside vectors)
  genre text,
  num_reviews int,
  mean_rating float,

  -- auxiliary columns (not indexed)
  +synopsis text
);

select
  movie_id,
  title,
  genre,
  num_reviews,
  mean_rating,
  distance
from vec_movies
where synopsis_embedding match '[...]'
  and genre = 'scifi'
  and num_reviews between 100 and 500
  and mean_rating > 3.5
  and k = 5;
/*
┌──────────┬─────────────────────┬─────────┬─────────────┬──────────────────┬──────────┐
│ movie_id │        title        │  genre  │ num_reviews │   mean_rating    │ distance │
├──────────┼─────────────────────┼─────────┼─────────────┼──────────────────┼──────────┤
│ 13       │ 'The Matrix'        │ 'scifi' │ 423         │ 4.5              │ 2.5      │
│ 18       │ 'Inception'         │ 'scifi' │ 201         │ 5.0              │ 2.5      │
│ 21       │ 'Gravity'           │ 'scifi' │ 342         │ 4.0              │ 5.5      │
│ 22       │ 'Dune'              │ 'scifi' │ 451         │ 4.40000009536743 │ 6.5      │
│ 8        │ 'Blade Runner 2049' │ 'scifi' │ 301         │ 5.0              │ 7.5      │
└──────────┴─────────────────────┴─────────┴─────────────┴──────────────────┴──────────┘
```

## Install

To try it out youself, download one of the following ZIP files that contain
pre-compiled SQLite extensions. You can manually load them into your
Python/JavaScript/Ruby/etc. projects to try things out.

| Platform                                                | Link                                                                                                                                                                            |
| ------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MacOS ARM                                               | [`sqlite-vec-macos-aarch64-extension.zip`](https://fly.storage.tigris.dev/sqlite-vec-public-static/metadata-filtering-beta/v1-052ba4b/sqlite-vec-macos-aarch64-extension.zip)   |
| MacOS x86_64                                            | [`sqlite-vec-macos-x86_64-extension.zip`](https://fly.storage.tigris.dev/sqlite-vec-public-static/metadata-filtering-beta/v1-052ba4b/sqlite-vec-macos-x86_64-extension.zip)     |
| Linux ARM                                               | [`sqlite-vec-linux-aarch64-extension.zip`](https://fly.storage.tigris.dev/sqlite-vec-public-static/metadata-filtering-beta/v1-052ba4b/sqlite-vec-linux-aarch64-extension.zip)   |
| Linux x86_64                                            | [`sqlite-vec-linux-x86_64-extension.zip`](https://fly.storage.tigris.dev/sqlite-vec-public-static/metadata-filtering-beta/v1-052ba4b/sqlite-vec-linux-x86_64-extension.zip)     |
| Windows x86_64                                          | [`sqlite-vec-windows-x86_64-extension.zip`](https://fly.storage.tigris.dev/sqlite-vec-public-static/metadata-filtering-beta/v1-052ba4b/sqlite-vec-windows-x86_64-extension.zip) |
| Cosmopolitan (`sqlite3` CLI with `sqlite-vec` baked in) | [`sqlite-vec-cosmopolitan.zip`](https://fly.storage.tigris.dev/sqlite-vec-public-static/metadata-filtering-beta/v1-052ba4b/sqlite-vec-cosmopolitan.zip)                         |

To check which experimental version you are on, run `SELECT vec_version()`. The
most recent version is `v-metadata-experiment.01`.

The rest of this document is documentation about how to use these new metadata,
auxiliary, and partition columns in these experimental builds.

## Experimental Status

This work isn't complete yet, so there are some subtle bugs and TODOs:

- You cannot `UPDATE` a `PARTITION KEY` value yet.
- KNN queries with a `WHERE` constraint on a `TEXT` metadata column that's
  longer than `12` characters will fail.
- `NULL` values are not allowed on metadata columns
- `PARTITION KEY` columns only support `=` operators currently, but `!=`, `<=`, `>=`, `<`, and `>` will operators will be supported.

These will be fixed before the official release.

## Metadata in `vec0` Virtual Tables

There are three ways to store non-vector columns in `vec0` virtual tables:
metadata columns, partition keys, and auxiliary columns. Each options has their
own benefits and limitations.

```sql
create virtual table vec_chunks using vec0(
  document_id integer partition key,
  contents_embedding float[768],

  -- partition key column, denoted by 'partition key'
  user_id integer partition key,

  -- metadata column, appears as normal column definition
  label text,

  -- auxiliary column, denoted by '+'
  +contents text
);
```

A quick summary of each option:

| Column Type       | Description                                                             | Benefits                                             | Limitations                                                                                                           |
| ----------------- | ----------------------------------------------------------------------- | ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Metadata columns  | Stores boolean, integer, floating point, or text data alongside vectors | Can be included in the `WHERE` clause of a KNN query | Slower full scan, slightly inefficient with long strings (`> 12` characters)                                          |
| Auxiliary columns | Stores any kind of data in a separate internal table                    | Eliminates need for an external `JOIN`               | Cannot appear in the `WHERE` clause of a KNN query                                                                    |
| Partition Key     | Internally shards vector index on a given key                           | Make selective queries much faster                   | Can cause oversharding and slow KNN if not used carefully. Should be +100's of vectors per unique partition key value |

### Metadata Columns

Metadata columns are extra "regular" columns that you can include in a `vec0`
table definition. These columns will be indexed along with declared vector
columns, and allow you to include extra `WHERE` constraints during KNN queries.

```sql
create virtual table vec_movies using vec0(
  movie_id integer primary key,
  synopsis_embedding float[1024],
  genre text,
  num_reviews int,
  mean_rating float,
  contains_violence boolean
);
```

In the `vec0` constructor, the `genre`, `num_reviews`, `mean_rating`, and
`contains_violence` columns are metadata columns, with their specified type.

A sample KNN query on this table could look like:

```sql
select *
from vec_movies
where synopsis_embedding match '[...]'
  and k = 5
  and genre = 'scifi'
  and num_reviews between 100 and 500
  and mean_rating > 3.5
  and contains_violence = false;
```

The first two conditions in the `WHERE` clause (`synopsis_embedding match` and
`k = 5`) denote that the query in a KNN query. The other conditions are metadata
constraints, that `sqlite-vec` will recognize and apply during the KNN
calculation. In other words, for the above query, a maximum of 5 rows would be
returned, all of which would fit under all the `WHERE` constraints for their
metadata column values.

#### Metadata Column Declaration

Metatadata columns are declared in the `vec0` constructor just like regular column definitions, with the column name first then the column type.

Only the following column types are supported in metadata columns. All these
columns are strictly typed.

- `TEXT` for text and strings
- `INTEGER` for 8-byte integers
- `FLOAT` for 8-byte floating-point numbers
- `BOOLEAN` for 1-bit `0` or `1`

Other column types may be supported in the future. Column type names are case
insensitive.

Additional column constraints like `UNIQUE` or `NOT NULL` are not supported.

A maximum of 16 metadata columns can be declared in a `vec0` virtual table.


#### Supported operations

Metadata column `WHERE` conditions in a KNN query will only work on the
following operators:

- `=` Equals to
- `!=` Not equals to
- `>` Greater than
- `>=` Greater than or equal to
- `<` Less than
- `<=` Less than or equal to

Using any other operator like `IS NULL`, `LIKE`, `GLOB`, `REGEXP`, or any scalar
function will result in an error or incorrect results.

Boolean columns only support `=` and `!=` operators.

### Partition Key Columns

Partition key columns allow one to internally shard a vector indexed based on a given key. Any `=` constraint in a `WHERE` clause on a partition key column will

For example, say you're performing vector search on a large dataset of documents. However, each document belongs to a user, and users can only search their own documents. It would be wasteful to perform a brute-force over all documents if you only care about 1 user at a time. So, you can partition the vector index based on user ID like so:

```sql
create virtual table vec_documents using vec0(
  document_id integer primary key,
  user_id integer partition key,
  contents_embedding float[1024]
)
```

Then during a KNN query, you can constrain results to a specific user in the `WHERE` clause like so:

```sql
select
  document_id,
  user_id,
  distance
from vec_documents
where contents_embedding match :query
  and k = 20
  and user_id = 123;
```

`sqlite-vec` will recognize the `user_id = 123` constraint and pre-filter vectors during a KNN search. Vectors with the same partition key values are collocated together, so this is a fast operation.

Another example: say you're performing vector search on a large dataset of news headlines of the past 100 years. However, in your application, most users only want to search a subset of articles based on when they were written, like "in the past ten years" or "during the obama administration." You can paritition based on published date like so:

```sql
create virtual table vec_articles using vec0(
  article_id integer primary key,
  published_date text partition key,
  headline_embedding float[1024]
);
```

And a KNN query:

```sql
select
  article_id,
  published_date,
  distance
from vec_articles
where headline_embedding match :query
  and published_date between '2009-01-20' and '2017-01-20'; -- Obama administration
```

But be careful! over-using partition key columns can lead to over-sharding and slower KNN queries. As a rule of thumb, make sure that every unique partition key value has ~100's of vectors associated with it. In the above examples, make sure that every user has on the magnitude of dozens or hundreds of documents each, or that every article has dozens or hundreds of articles per day. If they don't and you're noticing slow queries, try a more broad partition key value, like `organization_id` or `published_month`.

A maximum of 4 partition key columns can be declared in a `vec0` virtual table, but use caution if you find yourself using more than 1. Vectors are sharded along each unique combination, so over-sharding is more common with more partition key columns.

### Auxiliary Columns

Auxiliary columns store additional unindexed data separate from the internal vector index. They are meant for larger metadata that will never appear in a `WHERE` clause of a KNN query, eliminating the need for a separate `JOIN`.

Auxiliary columns are denoted by a `+` prefix in their column definition, like so:

```sql
create virtual table vec_chunks using vec0(
  contents_embedding float[1024],
  +contents text
);

select
  rowid,
  contents,
  distance
from vec_chunks
where contents_embedding match :query
  and k = 10;
```

Here we store the text contents of each chunk in the `contents` auxiliary column. When we perform a KNN query, we can reference the `contents` column in the `SELECT` clause, to get the raw text contents of the most relevant chunks.

A similar approach can be used for image embeddings:

```sql
create virtual table vec_image_chunks using vec0(
  image_embedding float[1024],
  +image blob
);

select
  rowid,
  contents,
  distance
from vec_chunks
where contents_embedding match :query
  and k = 10;
```

Here the `image` auxiliary column can store the raw image file in a large `BLOB` column. It can appear in the `SELECT` clause of the KNN query, to get the most relevant raw images.

In general, auxiliary columns are good for large text, blobs, URLs, or other datatypes that won't be a part of a `WHERE` clause of a KNN query. If you column will often appear in a `SELECT` clause but not the `WHERE` clause, then auxiliary columns are a good fit.

A maximum of 16 auxiliary columns can be declared in a `vec0` virtual table.
