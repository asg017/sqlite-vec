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
      link: /getting-started
    - theme: alt
      text: API Reference
      link: /api-reference

features:
  - title: Runs everywhere
    details: On the server, in the browser with WASM, mobile devices, and more!
  - title: Bindings for many languages
    details: Python, Ruby, Node.js/Deno/Bun, Go, Rust, and more!
  - title: Only SQL
    details: No extra configuration or server, only CREATEs, INSERTs, and SELECTs
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

---

<div class="language-sqlite vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">sqlite</span><pre class="shiki shiki-themes github-light github-dark vp-code"><code><span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">create</span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;"> virtual</span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;"> table</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec_movies </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">using</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec0(</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">  synopsis_embedding </span><span  id="xxx"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">float</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">[768]</span></span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">);</span></span>
<span class="line"></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">insert into</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec_movies(rowid, synopsis_embedding)</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">  select</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">    rowid,</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">    embed(synopsis) </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">as</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> synopsis_embedding</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">  from</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> movies;</span></span>
<span class="line"></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">select</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> rowid, distance</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">from</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec_movies</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">where</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> synopsis_embedding </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">match</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> embed(</span><span style="--shiki-light:#032F62;--shiki-dark:#9ECBFF;">'scary futuristic movies'</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">)</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">order by</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> distance</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">limit</span><span style="--shiki-light:#005CC5;--shiki-dark:#79B8FF;"> 20</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">;</span></span></code></pre></div>

<script>
  //document.querySelector('#xxx').style.background = 'red'
</script>

<script setup>
import { onMounted } from 'vue'

onMounted(() => {
  document.querySelector('#xxx').style.background = 'red';
});
</script>
