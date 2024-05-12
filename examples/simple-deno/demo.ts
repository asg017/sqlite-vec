import { Database } from "jsr:@db/sqlite@0.11";
import * as sqliteVec from "npm:sqlite-vec@0.0.1-alpha.9";

const db = new Database(":memory:");
db.enableLoadExtension = true;
sqliteVec.load(db);
db.enableLoadExtension = false;

const [sqlite_version, vec_version] = db
  .prepare("select sqlite_version(), vec_version()")
  .value<[string, string]>()!;
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
  "INSERT INTO vec_items(rowid, embedding) VALUES (?, ?)"
);

const insertVectors = db.transaction((items) => {
  for (const [id, vector] of items) {
    insertStmt.run(BigInt(id), new Uint8Array(new Float32Array(vector).buffer));
  }
});

insertVectors(items);

const rows = db
  .prepare(
    `
  SELECT
    rowid,
    distance
  FROM vec_items
  WHERE embedding MATCH ?
  ORDER BY distance
  LIMIT 5
`
  )
  .all([new Uint8Array(new Float32Array(query).buffer)]);

console.log(rows);

db.close();
