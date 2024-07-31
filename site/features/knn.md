# KNN queries

## `vec0` virtual tables

## Manually with `vec_distance_l2()`


```sql
create table items(
  contents text,
  contents_embedding float[768] (check vec_f32(contents_embedding))
);
```

## Static Blobs
