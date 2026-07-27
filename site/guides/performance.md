# Performance tuning

This page describes the knobs sqlite-vec exposes for tuning the on-disk and
in-memory performance of `vec0` tables.

## `chunk_size` table option

`vec0` stores vectors in fixed-size chunks. Each chunk holds up to
`chunk_size` vectors in a contiguous run, plus a per-chunk rowid array and a
bit-packed validity bitmap. sqlite-vec allocates a new chunk for a partition
only when the current chunk is full, and a chunk is reclaimed only when every
slot in it has been deleted or moved.

The `chunk_size` is a table-level option set with `CREATE VIRTUAL TABLE`:

```sql
CREATE VIRTUAL TABLE items USING vec0(
  embedding float[1536],
  chunk_size=64
);
```

### Contract

- **Default**: `1024`.
- **Range**: `8` to `4096`, inclusive.
- **Constraint**: must be divisible by `8`.
- **Scope**: per vec0 table. Two `vec0` tables in the same database may
  declare different `chunk_size` values; the choice does not leak between
  tables.
- **Immutability**: the option is set at `CREATE VIRTUAL TABLE` time and is
  fixed for the lifetime of the table. To change it, drop and recreate the
  table.

Invalid values raise an error at `CREATE VIRTUAL TABLE` time:

```sql
-- too large
CREATE VIRTUAL TABLE v USING vec0(a float[4], chunk_size=8200);
-- vec0 constructor error: chunk_size too large

-- not divisible by 8
CREATE VIRTUAL TABLE v USING vec0(a float[4], chunk_size=7);
-- vec0 constructor error: chunk_size must be a multiple of 8

-- malformed
CREATE VIRTUAL TABLE v USING vec0(a float[4], chunk_sizex=100);
-- Unknown table option: chunk_sizex
```

### Choosing a value

A smaller `chunk_size` reduces the empty-slot waste at the tail of the last
chunk in a partition. A larger `chunk_size` reduces the per-chunk metadata
overhead (the rowid array and validity bitmap are sized per chunk, not per
vector).

| Workload characteristic                      | Recommended `chunk_size` |
| -------------------------------------------- | ------------------------ |
| Dense, large corpus (>10k vectors/partition) | `1024` (default)         |
| Sparse per-partition corpus (<1k)            | `64`–`128`               |
| Small test corpus at minimum size            | `8` (minimum)            |

The on-disk savings from a smaller `chunk_size` come entirely from the
unused slots at the end of the last chunk in each partition. Once a
partition is dense, reducing `chunk_size` no longer saves space and only
increases the metadata overhead.

### Per-table shadow storage

Each `vec0` table owns its own shadow tables (`<name>_chunks`,
`<name>_info`, `<name>_rowids`, `<name>_vector_chunks00`,
`<name>_auxiliary`). With a different `chunk_size`, the shadow tables for
each table differ in row count and bitmap width, but they do not interact.
Creating two `vec0` tables with different `chunk_size` values in the same
database does not cause shared state or contention between them.
