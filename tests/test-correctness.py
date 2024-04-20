import sqlite3
import json

db = sqlite3.connect("test2.db")
db.enable_load_extension(True)
db.load_extension("dist/vec0")
db.enable_load_extension(False)
db.row_factory = sqlite3.Row
db.execute('attach database "sift1m-base.db" as sift1m')


#def test_sift1m():
rows = db.execute(
  '''
    with q as (
      select rowid, vector, k100 from sift1m.sift1m_query limit 10
    ),
    results as (
      select
        q.rowid as query_rowid,
        vec_sift1m.rowid as vec_rowid,
        distance,
        k100 as k100_groundtruth
      from q
      join vec_sift1m
      where
        vec_sift1m.vector match q.vector
        and k = 100
      order by distance
    )
    select
      query_rowid,
      json_group_array(vec_rowid order by distance) as topk,
      k100_groundtruth,
      json_group_array(vec_rowid order by distance) == k100_groundtruth
    from results
    group by 1;
  ''').fetchall()

results = []
for row in rows:
  actual = json.loads(row["topk"])
  expected = json.loads(row["k100_groundtruth"])

  ncorrect = sum([x in expected for x in actual])
  results.append(ncorrect / 100.0)

from statistics import mean
print(mean(results))
