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
      link: /markdown-examples
    - theme: alt
      text: API Reference
      link: /api-reference

features:
  - title: Runs everywhere
    details: On the server, in the browser with WASM, mobile devices, and more!
  - title: Bindings for many languages
    details: Python, Ruby, Node.js/Deno/Bun, Go, Rust, and more!
  - title: Only SQL
    details: No extra configuration or server, only CREATE/INSERT/SELECTs
---

```sqlite
create virtual table vec_movies using vec0(
  synopsis_embedding float[768]
);

insert into vec_movies(rowid, synopsis_embedding)
  select
    rowid,
    embed(synopsis) as synopsis_embedding
  from movies;

select rowid, distance
from vec_movies
where synopsis_embedding match embed('scary futuristic movies')
order by distance
limit 20;
```
