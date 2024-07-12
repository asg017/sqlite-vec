---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "sqlite-vec"
  text: ""
  tagline: A vector search SQLite extension that runs anywhere!
  actions:
    - theme: brand
      text: Getting Started
      link: /introduction
    - theme: alt
      text: API Reference
      link: /api-reference

features:
  - title: Runs everywhere
    details: On laptops, servers, mobile devices, browsers with WASM, Raspberry Pis, and more!
  - title: Bindings for many languages
    details: Python, Ruby, Node.js/Deno/Bun, Go, Rust, and more!
  - title: Pure SQL
    details: No extra configuration or server required — only CREATE, INSERT, and SELECT statements
---

```sqlite
-- store 768-dimensional vectors in a vec0 virtual table
create virtual table vec_movies using vec0(
  synopsis_embedding float[768]
);

-- insert vectors into the table, as JSON or compact BLOBs
insert into vec_movies(rowid, synopsis_embedding)
  select
    rowid,
    embed(synopsis) as synopsis_embedding
  from movies;

-- KNN search!
select
  rowid,
  distance
from vec_movies
where synopsis_embedding match embed('scary futuristic movies')
order by distance
limit 20;
```
