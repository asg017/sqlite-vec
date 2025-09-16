import { DatabaseSync } from "node:sqlite";
import * as sqliteVec from "sqlite-vec";

const db = new DatabaseSync(":memory:", { allowExtension: true });
sqliteVec.load(db);

const embedding = new Float32Array([0.1, 0.2, 0.3, 0.4]);
const { result } = db
  .prepare("select vec_length(?) as result")
  .get(new Uint8Array(embedding.buffer));

console.log(result); // 4