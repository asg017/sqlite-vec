async function main() {
  const { default: init } = await import("../dist/.wasm/sqlite3.mjs");
  const sqlite3 = await init();
  const vec_version = new sqlite3.oo1.DB(":memory:").selectValue(
    "select vec_version()",
  );
  console.log(vec_version);
}

main();
