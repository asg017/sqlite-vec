import numpy as np
import numpy.typing as npt
import time
import tqdm
import pytest

def cosine_similarity(
    vec: npt.NDArray[np.float32], mat: npt.NDArray[np.float32], do_norm: bool = True
) -> npt.NDArray[np.float32]:
    sim = vec @ mat.T
    if do_norm:
        sim /= np.linalg.norm(vec) * np.linalg.norm(mat, axis=1)
    return sim

def distance_l2(
    vec: npt.NDArray[np.float32], mat: npt.NDArray[np.float32]
) -> npt.NDArray[np.float32]:
    return np.sqrt(np.sum((mat - vec) ** 2, axis=1))


def topk(
    vec: npt.NDArray[np.float32],
    mat: npt.NDArray[np.float32],
    k: int = 5,
) -> tuple[npt.NDArray[np.int32], npt.NDArray[np.float32]]:
    distances = distance_l2(vec, mat)
    # Rather than sorting all similarities and taking the top K, it's faster to
    # argpartition and then just sort the top K.
    # The difference is O(N logN) vs O(N + k logk)
    indices = np.argpartition(distances, kth=k)[:k]
    top_indices = indices[np.argsort(distances[indices])]
    return top_indices, distances[top_indices]



vec = np.array([1.0, 2.0, 3.0], dtype=np.float32)
mat = np.array([
   [4.0, 5.0, 6.0],
   [1.0, 2.0, 1.0],
   [7.0, 8.0, 9.0]
], dtype=np.float32)
indices, distances = topk(vec, mat, k=2)
print(indices)
print(distances)

import sqlite3
import json
db = sqlite3.connect(":memory:")
db.enable_load_extension(True)
db.load_extension("../../dist/vec0")
db.execute("select load_extension('../../dist/vec0', 'sqlite3_vec_fs_read_init')")
db.enable_load_extension(False)

results = db.execute(
   '''
     select
      key,
      --value,
      vec_distance_l2(:q, value) as distance
    from json_each(:base)
    order by distance
    limit 2
   ''',
   {
      'base': json.dumps(mat.tolist()),
      'q': '[1.0, 2.0, 3.0]'
   }).fetchall()
a = [row[0] for row in results]
b = [row[1] for row in results]
print(a)
print(b)


#import sys; sys.exit()

db.execute('PRAGMA page_size=16384')

print("Loading into sqlite-vec vec0 table...")
t0 = time.time()
db.execute("create virtual table v using vec0(a float[3072], chunk_size=16)")
db.execute('insert into v select rowid, vector from vec_npy_each(vec_npy_file("dbpedia_openai_3_large_00.npy"))')
print(time.time() - t0)

print("loading numpy array...")
t0 = time.time()
base = np.load('dbpedia_openai_3_large_00.npy')
print(time.time() - t0)

np.random.seed(1)
queries = base[np.random.choice(base.shape[0], 20, replace=False), :]

np_durations = []
vec_durations = []
from random import randrange

def test_all():
  for idx, query in tqdm.tqdm(enumerate(queries)):
    #k = randrange(20, 1000)
    #k = 500
    k = 10

    t0 = time.time()
    np_ids, np_distances = topk(query, base, k=k)
    np_durations.append(time.time() - t0)

    t0 = time.time()
    rows = db.execute('select rowid, distance from v where a match ? and k = ?', [query, k]).fetchall()
    vec_durations.append(time.time() - t0)

    vec_ids = [row[0] for row in rows]
    vec_distances = [row[1] for row in rows]

    assert vec_distances == np_distances.tolist()
    #assert vec_ids == np_ids.tolist()
    #if (vec_ids != np_ids).any():
    #    print('idx', idx)
    #    print('query', query)
    #    print('np_ids', np_ids)
    #    print('np_distances', np_distances)
    #    print('vec_ids', vec_ids)
    #    print('vec_distances', vec_distances)
    #    raise Exception(idx)

  print('final', 'np' ,np.mean(np_durations), 'vec', np.mean(vec_durations))
