import { Database } from "bun:sqlite";
Database.setCustomSQLite("/usr/local/opt/sqlite3/lib/libsqlite3.dylib");

const db = new Database(":memory:");
//sqliteVec.load(db);
db.loadExtension("../../dist/vec0");

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
  "INSERT INTO vec_items(rowid, embedding) VALUES (?, vec_f32(?))",
);

const insertVectors = db.transaction((items) => {
  for (const [id, vector] of items) {
    insertStmt.run(BigInt(id), new Float32Array(vector));
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
  LIMIT 3
`,
  )
  .all(new Float32Array(query));

console.log(rows);
