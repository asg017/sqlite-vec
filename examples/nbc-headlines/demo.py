import sqlite3
import sqlite_vec
from sentence_transformers import SentenceTransformer

db = sqlite3.connect("articles.db")
db.enable_load_extension(True)
sqlite_vec.load(db)
db.enable_load_extension(False)

model = SentenceTransformer("all-MiniLM-L6-v2")


query = "sports"
query_embedding = model.encode(query)

results = db.execute(
  """
    select
      article_id,
      headline,
      distance
    from vec_articles
    left join articles on articles.id = vec_articles.article_id
    where headline_embedding match ?
      and k = 8;
  """,
  [query_embedding]
).fetchall()

for (article_id, headline, distance) in results:
  print(article_id, headline, distance)
