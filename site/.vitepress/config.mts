import { defineConfig, DefaultTheme, HeadConfig } from "vitepress";
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const PROJECT = "sqlite-vec";
const description = "A vector search SQLite extension that runs anywhere!";

const VERSION = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "..", "..", "VERSION"),
  "utf8"
);

function head(): HeadConfig[] {
  return [
    [
      "link",
      {
        rel: "shortcut icon",
        type: "image/svg+xml",
        href: "favicon.svg",
      },
    ],
    [
      "script",
      {
        defer: "",
        "data-domain": "alexgarcia.xyz/sqlite-vec",
        src: "https://plausible.io/js/script.js",
      },
    ],
  ];
}

const guides = {
  text: "Guides",
  items: [],
};

function nav(): DefaultTheme.NavItem[] {
  return [
    //guides,
    { text: "API Reference", link: "/api-reference" },
    { text: "♥ Sponsor", link: "https://github.com/sponsors/asg017" },
    {
      text: `v${VERSION}`,
      items: [
        {
          text: "Github Release",
          link: `https://github.com/asg017/${PROJECT}/releases/${VERSION}`,
        },
        {
          text: "Bindings",
          items: [
            {
              text: "Python: PyPi package",
              link: `https://pypi.org/project/${PROJECT}`,
            },
            {
              text: "Node.js: NPM package",
              link: `https://www.npmjs.com/package/${PROJECT}`,
            },
            {
              text: "Ruby: Ruby gem",
              link: `https://rubygems.org/gems/${PROJECT}`,
            },
            {
              text: "Rust: Cargo crate",
              link: `https://crates.io/crates/${PROJECT}`,
            },
            {
              text: "Golang: Go module",
              link: `https://pkg.go.dev/github.com/asg017/${PROJECT}/bindings/go/cgo`,
            },
            {
              text: "Datasette: Plugin",
              link: `https://datasette.io/plugins/datasette-${PROJECT}`,
            },
            {
              text: "sqlite-utils: Plugin",
              link: `https://datasette.io/plugins/datasette-${PROJECT}`,
            },
          ],
        },
      ],
    },
  ];
}

function sidebar(): DefaultTheme.SidebarItem[] {
  return [
    {
      text: "Getting Started",
      collapsed: false,
      items: [
        {
          text: "Quickstart",
          link: "/getting-started",
        },
      ],
    },
    {
      text: "Using with...",
      collapsed: false,
      items: [
        { text: "Python", link: "/python" },
        { text: "JavaScript (Server-side)", link: "/js" },
        { text: "JavaScript+WASM (Browser)", link: "/wasm-browser" },
        { text: "Ruby", link: "/ruby" },
        { text: "Rust", link: "/rust" },
        { text: "Go", link: "/go" },
        { text: "C/C++", link: "/c" },
        { text: "Datasette", link: "/datasette" },
        { text: "sqlite-utils", link: "/sqlite-utils" },
        { text: "Loadable Extension", link: "/loadable" },
      ],
    },
    //guides,
    {
      text: "Comparisons with...",
      link: "/compare",
    },
    {
      text: "Documentation",
      items: [
        { text: "Building from Source", link: "/building-source" },
        { text: "API Reference", link: "/api-reference" },
      ],
    },
    {
      text: "See also",
      items: [
        {
          text: "sqlite-ecosystem",
          link: "https://github.com/asg017/sqlite-ecosystem",
        },
      ],
    },
  ];
}

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: PROJECT,
  description,
  lastUpdated: true,
  head: head(),
  base: "/sqlite-vec/",
  themeConfig: {
    nav: nav(),

    sidebar: sidebar(),

    footer: {
      message: "MIT License",
      copyright: "Copyright © 2024 Alex Garcia",
    },
    outline: "deep",
    search: {
      provider: "local",
    },
    socialLinks: [
      { icon: "github", link: `https://github.com/asg017/${PROJECT}` },
      { icon: "discord", link: `https://discord.gg/jAeUUhVG2D` },
    ],
    editLink: {
      pattern: `https://github.com/asg017/${PROJECT}/edit/main/site/:path`,
    },
  },
  rewrites: {
    "using/:pkg.md": ":pkg.md",
    "guides/:pkg.md": ":pkg.md",
  },
  markdown: {
    languages: [
      JSON.parse(
        readFileSync(
          join(
            dirname(fileURLToPath(import.meta.url)),
            "..",
            "sqlite.tmlanguage.json"
          ),
          "utf8"
        )
      ),
    ],
  },
});
