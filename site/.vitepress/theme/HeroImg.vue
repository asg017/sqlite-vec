<script setup lang="ts"></script>

<template>
  <div
    style="
      background: var(--vp-c-default-3);
      padding: -4px 12px;
      border-radius: 10px;
    "
  >
    <div>
      <div class="language-sqlite vp-adaptive-theme">
        <pre
          class="shiki shiki-themes github-light github-dark vp-code"
        ><code><span class="line"><span style="--shiki-light:#6A737D;--shiki-dark:#6A737D;">-- store 768-dimensional vectors in a vec0 virtual table</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">create</span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;"> virtual</span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;"> table</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec_movies </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">using</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec0(</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">  synopsis_embedding </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">float</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">[768]</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">);</span></span>
<span class="line"></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-dark:#6A737D;">-- insert vectors into the table, as JSON or compact BLOBs</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">insert into</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec_movies(rowid, synopsis_embedding)</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">  select</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">    rowid,</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">    embed(synopsis) </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">as</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> synopsis_embedding</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">  from</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> movies;</span></span>
<span class="line"></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-dark:#6A737D;">-- KNN search!</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">select</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">  rowid,</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">  distance</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">from</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> vec_movies</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">where</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> synopsis_embedding </span><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">match</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> embed(</span><span style="--shiki-light:#032F62;--shiki-dark:#9ECBFF;">'scary futuristic movies'</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">)</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">order by</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;"> distance</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#F97583;">limit</span><span style="--shiki-light:#005CC5;--shiki-dark:#79B8FF;"> 20</span><span style="--shiki-light:#24292E;--shiki-dark:#E1E4E8;">;</span></span></code></pre>
      </div>
    </div>
  </div>
</template>
