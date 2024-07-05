import numpy as np
import duckdb
db = duckdb.connect(":memory:")

result = db.execute(
"""
  select
    -- _id,
    -- title,
    -- text as contents,
    embedding::float[] as embeddings
  from "hf://datasets/Supabase/dbpedia-openai-3-large-1M/dbpedia_openai_3_large_00.parquet"
"""
).fetchnumpy()['embeddings']

np.save("dbpedia_openai_3_large_00.npy", np.vstack(result))
