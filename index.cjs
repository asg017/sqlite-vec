const { join } = require("node:path");
const { arch, platform } = require("node:process");
const { statSync } = require("node:fs");

const ENTRYPOINT_BASE_NAME = "vec0";

function extensionSuffix(platform) {
  if (platform === "win32") return "dll";
  if (platform === "darwin") return "dylib";
  return "so";
}

function getLoadablePath() {
  const loadablePath = join(
    __dirname,
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

module.exports = { getLoadablePath, load };
