import { Database } from "bun:sqlite";

Database.setCustomSQLite("/usr/local/opt/sqlite3/lib/libsqlite3.dylib");

const db = new Database(":memory:");
//sqliteVec.load(db);
db.loadExtension("../../dist/vec0");

const { sqlite_version, vec_version } = db
  .prepare(
    "select sqlite_version() as sqlite_version, vec_version() as vec_version;"
  )
  .get();

console.log(`sqlite_version=${sqlite_version}, vec_version=${vec_version}`);

db.exec("CREATE VIRTUAL TABLE vec_items USING vec0(embedding float[8])");

const insertStmt = db.prepare(
  "INSERT INTO vec_items(rowid, embedding) VALUES (?, vec_f32(?))"
);

const insertVectors = db.transaction((items) => {
  for (const [id, vector] of items) {
    insertStmt.run(BigInt(id), vector);
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
  .all(query);

console.log(rows);
