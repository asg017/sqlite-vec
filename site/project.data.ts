import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROJECT = "sqlite-vec";

const VERSION = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "..", "VERSION"),
  "utf8",
);

export default {
  load() {
    return {
      PROJECT,
      VERSION,
    };
  },
};
