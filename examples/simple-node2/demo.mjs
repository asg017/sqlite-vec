/**
 * This demo Node.js script shows how you can use sqlite-vec with
 * the new builtin node:sqlite module.
 * Note that this requires Node v23.5.0 or above.
 */
import { DatabaseSync } from "node:sqlite";
import * as sqliteVec from "sqlite-vec";

// allExtension is required to enable extension support
const db = new DatabaseSync(":memory:", { allowExtension: true });
sqliteVec.load(db);

const { sqlite_version, vec_version } = db
  .prepare(
    "select sqlite_version() as sqlite_version, vec_version() as vec_version;",
  )
  .get();

console.log(`sqlite_version=${sqlite_version}, vec_version=${vec_version}`);

const items = [
  [1, [0.1, 0.1, 0.1, 0.1]],
  [2, [0.2, 0.2, 0.2, 0.2]],
  [3, [0.3, 0.3, 0.3, 0.3]],
  [4, [0.4, 0.4, 0.4, 0.4]],
  [5, [0.5, 0.5, 0.5, 0.5]],
];
const query = [0.3, 0.3, 0.3, 0.3];

db.exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[4])");

const insertStmt = db.prepare(
  "INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)",
);

// TODO node:sqlite doesn't have `.transaction()` support yet
for (const [id, vector] of items) {
  // node:sqlite requires Uint8Array for BLOB values, so a bit awkward
  insertStmt.run(BigInt(id), new Uint8Array(new Float32Array(vector).buffer));
}

const rows = db
  .prepare(
    `
  SELECT
    rowid,
    distance
  FROM vec_items
  WHERE embedding MATCH ?
  ORDER BY distance
  LIMIT 3
`,
  )
  .all(new Uint8Array(new Float32Array(query).buffer));

console.log(rows);
