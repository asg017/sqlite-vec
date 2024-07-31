import Database from "better-sqlite3";
import * as sqliteVec from "sqlite-vec";
import { pipeline } from "@xenova/transformers";

const db = new Database("articles.db");
sqliteVec.load(db);

const extractor = await pipeline(
  "feature-extraction",
  "Xenova/all-MiniLM-L6-v2"
);

const query = "sports";
const queryEmbedding = await extractor([query], {
  pooling: "mean",
  normalize: true,
});

const rows = db
  .prepare(
    `
  select
    article_id,
    headline,
    distance
  from vec_articles
  left join articles on articles.id = vec_articles.article_id
  where headline_embedding match ?
    and k = 8;
`
  )
  .all(queryEmbedding.data);

console.log(rows);
