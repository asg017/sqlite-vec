import { DefaultTheme, defineConfig, HeadConfig } from "vitepress";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROJECT = "sqlite-vec";
const description = "A vector search SQLite extension that runs anywhere!";

const VERSION = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "..", "..", "VERSION"),
  "utf8"
);

const sqliteLanuage = JSON.parse(
  readFileSync(
    join(
      dirname(fileURLToPath(import.meta.url)),
      "..",
      "sqlite.tmlanguage.json"
    ),
    "utf8"
  )
);

function head(): HeadConfig[] {
  return [
    [
      "link",
      {
        rel: "shortcut icon",
        type: "image/svg+xml",
        href: "./logo.light.svg",
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
  collapsed: true,
  items: [
    { text: "Performance", link: "/guides/performance" },

    {
      text: "Vector operations",
      items: [
        { text: "Vector Arithmetic", link: "/guides/arithmetic" },
        { text: "Binary Quantization", link: "/guides/binary-quant" },
        { text: "Scalar Quantization", link: "/guides/scalar-quant" },
        {
          text: "Matryoshka Embeddings",
          link: "/guides/matryoshka",
        },
      ],
    },

    /* {
      text: "Build with sqlite-vec",
      items: [
        { text: "Semantic Search", link: "/guides/semantic-search" },
        { text: "Hybrid Search", link: "/guides/hybrid-search" },
        { text: "Retrival Augmented Generation (RAG)", link: "/guides/rag" },
        { text: "Classifiers", link: "/guides/classifiers" },
      ],
    },*/
  ],
};

function nav(): DefaultTheme.NavItem[] {
  return [
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
              text: "Golang: Go module (CGO)",
              link: `https://pkg.go.dev/github.com/asg017/${PROJECT}-go-bindings/cgo`,
            },
            {
              text: "Golang: Go module (WASM ncruces)",
              link: `https://pkg.go.dev/github.com/asg017/${PROJECT}-go-bindings/ncruces`,
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
      collapsed: true,
      items: [
        {
          text: "Introduction",
          link: "/introduction",
        },
        {
          text: "Installation",
          link: "/installation",
        },
      ],
    },

    {
      text: "Using with...",
      collapsed: true,
      items: [
        { text: "Python", link: "/python" },
        { text: "JavaScript", link: "/js" },
        { text: "Ruby", link: "/ruby" },
        { text: "Rust", link: "/rust" },
        { text: "Go", link: "/go" },
        { text: "C/C++", link: "/c" },
        { text: "Browser (WASM)", link: "/wasm" },
        { text: "Datasette", link: "/datasette" },
        { text: "sqlite-utils", link: "/sqlite-utils" },
        { text: "rqlite", link: "/rqlite" },
        { text: "Android+iOS", link: "/android-ios" },
      ],
    },
    {
      text: "Features",
      collapsed: true,
      items: [
        { text: "Vector formats", link: "/features/vector-formats" },
        { text: "KNN queries", link: "/features/knn" },
        { text: "vec0 Virtual Tables", link: "/features/vec0" },
        //{ text: "Static blobs", link: "/features/static-blobs" },
      ],
    },
    guides,
    {
      text: "Documentation",
      items: [
        { text: "Compiling", link: "/compiling" },
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
        {
          text: "sqlite-lembed",
          link: "https://github.com/asg017/sqlite-lembed",
        },
        {
          text: "sqlite-rembed",
          link: "https://github.com/asg017/sqlite-rembed",
        },
      ],
    },
  ];
}

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: `${PROJECT}`,
  description,
  lastUpdated: true,
  head: head(),
  base: "/sqlite-vec/",
  themeConfig: {
    logo: {
      light: "/logo.dark.svg",
      dark: "/logo.light.svg",
      alt: "sqlite-vec logo",
    },

    nav: nav(),
    sidebar: sidebar(),
    footer: {
      message: "MIT/Apache-2 License",
      copyright:
        'Copyright © 2024 <a href="https://alexgarcia.xyz/">Alex Garcia</a>',
    },
    outline: "deep",
    search: {
      provider: "local",
    },
    socialLinks: [
      { icon: "github", link: `https://github.com/asg017/${PROJECT}` },
      { icon: "discord", link: `https://discord.gg/Ve7WeCJFXk` },
    ],
    editLink: {
      pattern: `https://github.com/asg017/${PROJECT}/edit/main/site/:path`,
    },
  },
  rewrites: {
    "using/:pkg.md": ":pkg.md",
    "getting-started/:pkg.md": ":pkg.md",
    //"guides/:pkg.md": ":pkg.md",
  },
  markdown: {
    languages: [sqliteLanuage],
  },
});
