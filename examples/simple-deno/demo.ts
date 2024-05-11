import { Database } from "jsr:@db/sqlite@0.11";
//import { loadablePath } from "npm:sqlite-vec";

const db = new Database(":memory:");
db.enableLoadExtension = true;
db.loadExtension("../../dist/vec0");
db.enableLoadExtension = false;

const [sqlite_version, vec_version] = db
  .prepare("select sqlite_version(), vec_version()")
  .value<[string]>()!;
console.log(`sqlite_version=${sqlite_version}, vec_version=${vec_version}`);

db.exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[8])");

const insertStmt = db.prepare(
  "INSERT INTO vec_items(rowid, embedding) VALUES (?1, vec_f32(?2))"
);

const insertVectors = db.transaction((items) => {
  for (const [id, vector] of items) {
    insertStmt.run(BigInt(id), new Uint8Array(vector.buffer));
  }
});

insertVectors([
  [1, new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8])],
  [2, new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8])],
]);

const query = new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]);
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
  .all([new Uint8Array(query.buffer)]);

console.log(rows);

db.close();
