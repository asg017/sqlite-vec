import { defineConfig } from "vitepress";
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const PROJECT = "sqlite-vec";
const VERSION = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "..", "VERSION"),
  "utf8"
);

export default {
  load() {
    return {
      PROJECT,
      VERSION,
    };
  },
};
