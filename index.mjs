import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { arch, platform } from "node:process";
import { statSync } from "node:fs";

const ENTRYPOINT_BASE_NAME = "vec0";

function extensionSuffix(platform) {
  if (platform === "win32") return "dll";
  if (platform === "darwin") return "dylib";
  return "so";
}

function getLoadablePath() {
  const loadablePath = join(
    fileURLToPath(new URL(".", import.meta.url)),
    "dist",
    `${ENTRYPOINT_BASE_NAME}.${extensionSuffix(platform)}`
  );

  if (!statSync(loadablePath, { throwIfNoEntry: false })) {
    throw new Error(`Loadable extension for sqlite-vec not found at ${loadablePath}. Was the extension built? Run: make loadable`);
  }

  return loadablePath;
}

function load(db) {
  db.loadExtension(getLoadablePath());
}

export { getLoadablePath, load };
