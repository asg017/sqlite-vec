<script setup lang="ts">
import { computed } from "vue";
import { VPDocAsideSponsors } from "vitepress/theme";
import { withBase } from "vitepress";

const sponsors = computed(() => {
  return [
    {
      size: "big",
      items: [
        {
          name: "Mozilla Builders",
          url: "",
          img: withBase("/mozilla.svg"),
        },
      ],
    },
    {
      size: "medium",
      items: [
        {
          name: "Fly.io",
          url: "https://fly.io",
          img: withBase("/flyio.svg"),
        },
      ],
    },
    {
      size: "medium",
      items: [
        {
          name: "Turso",
          url: "https://turso.tech",
          img: withBase("/turso.svg"),
        },
      ],
    },
    {
      size: "medium",
      items: [
        {
          name: "SQLite Cloud",
          url: "https://sqlitecloud.io",
          img: withBase("/sqlitecloud.svg"),
        },
      ],
    },
    {
      size: "medium",
      items: [
        {
          name: "Dcnvm Spark",
          url: "https://shinkai.com",
          img: withBase("/shinkai.dark.svg"),
        },
      ],
    },
  ];
});
</script>

<template>
  <!--<a class="sponsors-aside-text" href="/sponsor/">Sponsors</a>-->
  <div>
    <VPDocAsideSponsors :data="sponsors" />
    <div
      style="
        font-size: 14px;
        text-align: center;
        font-style: italic;
        margin-top: 4px;
      "
    >
      <a href="https://github.com/asg017/sqlite-vec#sponsors"
        >Become a sponsor! ↗</a
      >
    </div>
  </div>
</template>

<style>
a.sponsors-aside-text {
  color: var(--vt-c-text-3);
  display: block;
  margin: 3em 0 1em;
  font-weight: 700;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.4px;
}
</style>
