// https://vitepress.dev/guide/custom-theme
import { h } from "vue";
import type { Theme } from "vitepress";
import DefaultTheme from "vitepress/theme";
import "./style.css";

export default {
  extends: DefaultTheme,
  Layout: () => {
    return h(DefaultTheme.Layout, null, {
      // https://vitepress.dev/guide/extending-default-theme#layout-slots
      "layout-top": () =>
        h("marquee", { class: "banner", scrollamount: "10" }, [
          "🚧🚧🚧 sqlite-vec is still in beta, and this documentation is incomplete! Watch the repo for updates 🚧🚧🚧",
        ]),
    });
  },
  enhanceApp({ app, router, siteData }) {
    // ...
  },
} satisfies Theme;
