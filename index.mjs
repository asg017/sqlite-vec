
import { fileURLToPath } from "node:url";
import { arch, platform } from "node:process";


const BASE_PACKAGE_NAME = "sqlite-vec";
const ENTRYPOINT_BASE_NAME = "vec0";
const supportedPlatforms = [["darwin","x64"],["linux","x64"],["darwin","arm64"],["win32","x64"],["linux","arm64"],["android","arm64"]];

const invalidPlatformErrorMessage = `Unsupported platform for ${BASE_PACKAGE_NAME}, on a ${platform}-${arch} machine. Supported platforms are (${supportedPlatforms
  .map(([p, a]) => `${p}-${a}`)
  .join(",")}). Consult the ${BASE_PACKAGE_NAME} NPM package README for details.`;

const extensionNotFoundErrorMessage = packageName => `Loadble extension for ${BASE_PACKAGE_NAME} not found. Was the ${packageName} package installed?`;

function validPlatform(platform, arch) {
  return (
    supportedPlatforms.find(([p, a]) => platform === p && arch === a) !== undefined
  );
}
function extensionSuffix(platform) {
  if (platform === "win32") return "dll";
  if (platform === "darwin") return "dylib";
  return "so";
}
function platformPackageName(platform, arch) {
  let os = platform === "win32" ? "windows" : platform;
  if (os === "android") os = "linux";
  return `${BASE_PACKAGE_NAME}-${os}-${arch}`;
}

function getLoadablePath() {
  if (!validPlatform(platform, arch)) {
    throw new Error(
      invalidPlatformErrorMessage
    );
  }
  const packageName = platformPackageName(platform, arch);
  const loadablePath = fileURLToPath(import.meta.resolve(packageName + "/" + ENTRYPOINT_BASE_NAME + "." + extensionSuffix(platform)));
  return loadablePath;
}

function load(db) {
  const p = getLoadablePath();
  db.loadExtension(p.replace(/\.so$/, ''));
}

export {getLoadablePath, load};
