"""
Integration tests for the ANN indexes (IVF, DiskANN, rescore) using real
text embeddings from a local llama-server embedding endpoint.

Requires an OpenAI-compatible embedding server on localhost:2235
(e.g. `llama-server --embedding --port 2235`). All tests are skipped when
the server is unreachable, so this file is safe to run in CI.

Each index type is exercised end-to-end: insert real embeddings, build the
index, run semantic KNN queries, and compare against exact brute-force
nearest neighbors computed in numpy.
"""
import json
import math
import sqlite3
import struct
import urllib.request
import urllib.error

import pytest

EMBEDDING_URL = "http://localhost:2235/v1/embeddings"

# Small corpus with clear topic clusters, so semantic KNN has structure to find.
CORPUS = [
    # cooking
    "How to bake sourdough bread at home",
    "The best way to season a cast iron skillet",
    "A simple recipe for tomato pasta sauce",
    "Slow roasting vegetables brings out their sweetness",
    "Kneading dough develops gluten structure",
    "Marinate the chicken overnight for more flavor",
    "Fresh basil and oregano elevate Italian dishes",
    "Caramelizing onions takes patience and low heat",
    # programming
    "Debugging a segmentation fault in C code",
    "Python list comprehensions are concise and fast",
    "Writing unit tests improves software reliability",
    "The compiler optimizes away dead code branches",
    "Git rebase rewrites commit history",
    "SQL joins combine rows from multiple tables",
    "Memory leaks occur when allocations are never freed",
    "Concurrency bugs are hard to reproduce",
    # astronomy
    "The James Webb telescope observes distant galaxies",
    "Jupiter's great red spot is a giant storm",
    "Black holes warp spacetime around them",
    "The moon causes ocean tides on Earth",
    "Supernovae forge heavy elements in their cores",
    "Mars rovers search for signs of ancient water",
    "Comets have tails that point away from the sun",
    "Neutron stars are incredibly dense stellar remnants",
    # sports
    "The marathon runner kept a steady pace",
    "A hat-trick means scoring three goals in one game",
    "Tennis players train their serve for hours",
    "The basketball team practiced free throws",
    "Cyclists draft behind each other to save energy",
    "Swimmers shave milliseconds off their lap times",
    "The goalkeeper made a spectacular diving save",
    "Weightlifting requires strict form to avoid injury",
    # music
    "The orchestra tuned their instruments before the concert",
    "Jazz improvisation builds on chord progressions",
    "The guitarist replaced his worn-out strings",
    "A minor key often sounds sad or contemplative",
    "The drummer kept time with the metronome",
    "Choirs blend many voices into one harmony",
    "Vinyl records have made a surprising comeback",
    "The pianist practiced scales every morning",
    # nature
    "Bees pollinate flowers while gathering nectar",
    "The old oak tree survived the lightning strike",
    "Salmon swim upstream to their spawning grounds",
    "Autumn leaves turn red and gold before falling",
    "Coral reefs host thousands of marine species",
    "Wolves hunt in coordinated packs",
    "Moss grows on the shaded side of rocks",
    "Monarch butterflies migrate thousands of miles",
]

QUERIES = {
    "What's a good way to cook dinner tonight?": "cooking",
    "My program crashes with a memory error": "programming",
    "Telescopes and planets in outer space": "astronomy",
    "Athletes competing in a championship": "sports",
    "Playing melodies on a piano": "music",
    "Wildlife and forest ecosystems": "nature",
}


def _f32(values):
    return struct.pack("%df" % len(values), *values)


def _fetch_embeddings(texts):
    req = urllib.request.Request(
        EMBEDDING_URL,
        data=json.dumps({"input": texts, "model": "default"}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())
    out = [None] * len(texts)
    for item in data["data"]:
        out[item["index"]] = item["embedding"]
    assert all(e is not None for e in out)
    return out


def _server_available():
    try:
        _fetch_embeddings(["ping"])
        return True
    except (urllib.error.URLError, OSError):
        return False


if not _server_available():
    pytest.skip(
        "embedding server not reachable on localhost:2235", allow_module_level=True
    )


def _build_flags():
    con = sqlite3.connect(":memory:")
    con.enable_load_extension(True)
    con.load_extension("dist/vec0")
    return con.execute("SELECT vec_debug()").fetchone()[0].split("Build flags:")[-1]

_FLAGS = _build_flags()
needs_ivf = pytest.mark.skipif("ivf" not in _FLAGS, reason="IVF not compiled in")
needs_diskann = pytest.mark.skipif("diskann" not in _FLAGS, reason="DiskANN not compiled in")
needs_rescore = pytest.mark.skipif("rescore" not in _FLAGS, reason="rescore not compiled in")


@pytest.fixture(scope="module")
def emb():
    """Corpus and query embeddings, fetched once per module."""
    corpus_vecs = []
    for i in range(0, len(CORPUS), 16):
        corpus_vecs.extend(_fetch_embeddings(CORPUS[i : i + 16]))
    query_vecs = _fetch_embeddings(list(QUERIES.keys()))
    dim = len(corpus_vecs[0])
    assert all(len(v) == dim for v in corpus_vecs + query_vecs)
    return {"corpus": corpus_vecs, "queries": query_vecs, "dim": dim}


@pytest.fixture()
def db():
    con = sqlite3.connect(":memory:")
    con.row_factory = sqlite3.Row
    con.enable_load_extension(True)
    con.load_extension("dist/vec0")
    con.enable_load_extension(False)
    return con


def _l2_sq(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b))


def _exact_knn(corpus_vecs, query_vec, k):
    dists = sorted(
        (( _l2_sq(v, query_vec), rid) for rid, v in enumerate(corpus_vecs)),
    )
    return [rid for _, rid in dists[:k]]


def _recall(approx_ids, exact_ids):
    return len(set(approx_ids) & set(exact_ids)) / len(exact_ids)


def _topic_of(rowid):
    return ["cooking", "programming", "astronomy", "sports", "music", "nature"][
        rowid // 8
    ]


def _insert_corpus(db, table, emb):
    for rid, v in enumerate(emb["corpus"]):
        db.execute(f"INSERT INTO {table}(rowid, v) VALUES (?, ?)", [rid, _f32(v)])


def _knn_ids(db, table, query_vec, k):
    return [
        r[0]
        for r in db.execute(
            f"SELECT rowid FROM {table} WHERE v MATCH ? AND k = ?",
            [_f32(query_vec), k],
        ).fetchall()
    ]


# ============================================================================
# IVF
# ============================================================================


@needs_ivf
def test_ivf_untrained_is_exact(db, emb):
    """Before training, IVF scans the unassigned cells: results must be exact."""
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0(v float[{emb['dim']}] indexed by ivf(nlist=6))"
    )
    _insert_corpus(db, "t", emb)
    for qv in emb["queries"]:
        assert _knn_ids(db, "t", qv, 10) == _exact_knn(emb["corpus"], qv, 10)


@needs_ivf
def test_ivf_trained_recall_and_semantics(db, emb):
    """After k-means training, probing all lists must stay exact, and the
    top hit for each query must come from the query's semantic topic."""
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0(v float[{emb['dim']}] indexed by ivf(nlist=6, nprobe=6))"
    )
    _insert_corpus(db, "t", emb)
    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

    for (qtext, topic), qv in zip(QUERIES.items(), emb["queries"]):
        ids = _knn_ids(db, "t", qv, 10)
        assert ids == _exact_knn(emb["corpus"], qv, 10)
        assert _topic_of(ids[0]) == topic, f"top hit for {qtext!r} was {_topic_of(ids[0])}"


@needs_ivf
def test_ivf_partial_probe_recall(db, emb):
    """Probing 3 of 6 lists is approximate but should keep good recall@10
    on a clustered corpus."""
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0(v float[{emb['dim']}] indexed by ivf(nlist=6, nprobe=3))"
    )
    _insert_corpus(db, "t", emb)
    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

    recalls = [
        _recall(_knn_ids(db, "t", qv, 10), _exact_knn(emb["corpus"], qv, 10))
        for qv in emb["queries"]
    ]
    assert sum(recalls) / len(recalls) >= 0.6, f"mean recall too low: {recalls}"


@needs_ivf
def test_ivf_insert_after_training_and_delete(db, emb):
    """Vectors inserted after training go to real cells; deleted rows must
    disappear from results; exact search with full probing still holds."""
    half = len(emb["corpus"]) // 2
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0(v float[{emb['dim']}] indexed by ivf(nlist=4, nprobe=4))"
    )
    for rid in range(half):
        db.execute("INSERT INTO t(rowid, v) VALUES (?, ?)", [rid, _f32(emb["corpus"][rid])])
    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")
    for rid in range(half, len(emb["corpus"])):
        db.execute("INSERT INTO t(rowid, v) VALUES (?, ?)", [rid, _f32(emb["corpus"][rid])])

    deleted = set(range(0, len(emb["corpus"]), 5))
    for rid in deleted:
        db.execute("DELETE FROM t WHERE rowid = ?", [rid])

    alive = [rid for rid in range(len(emb["corpus"])) if rid not in deleted]
    for qv in emb["queries"]:
        ids = _knn_ids(db, "t", qv, 10)
        assert not (set(ids) & deleted)
        exact = [
            rid
            for rid in _exact_knn(emb["corpus"], qv, len(emb["corpus"]))
            if rid in set(alive)
        ][:10]
        assert ids == exact


@needs_ivf
def test_ivf_int8_oversample_recall(db, emb):
    """int8 quantization with oversample re-ranking should track exact
    results closely on real embeddings (values within [-1, 1])."""
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0("
        f"v float[{emb['dim']}] indexed by ivf(nlist=6, nprobe=6, quantizer=int8, oversample=4))"
    )
    _insert_corpus(db, "t", emb)
    db.execute("INSERT INTO t(t) VALUES ('compute-centroids')")

    recalls = [
        _recall(_knn_ids(db, "t", qv, 10), _exact_knn(emb["corpus"], qv, 10))
        for qv in emb["queries"]
    ]
    assert sum(recalls) / len(recalls) >= 0.8, f"mean recall too low: {recalls}"


# ============================================================================
# DiskANN
# ============================================================================


@needs_diskann
def test_diskann_recall_and_delete(db, emb):
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0("
        f"v float[{emb['dim']}] indexed by diskann(neighbor_quantizer=int8))"
    )
    _insert_corpus(db, "t", emb)

    recalls = []
    for (qtext, topic), qv in zip(QUERIES.items(), emb["queries"]):
        ids = _knn_ids(db, "t", qv, 10)
        recalls.append(_recall(ids, _exact_knn(emb["corpus"], qv, 10)))
        assert _topic_of(ids[0]) == topic
    assert sum(recalls) / len(recalls) >= 0.8, f"mean recall too low: {recalls}"

    deleted = set(range(0, len(emb["corpus"]), 4))
    for rid in deleted:
        db.execute("DELETE FROM t WHERE rowid = ?", [rid])
    for qv in emb["queries"]:
        assert not (set(_knn_ids(db, "t", qv, 15)) & deleted)


# ============================================================================
# rescore
# ============================================================================


@needs_rescore
def test_rescore_int8_recall(db, emb):
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0("
        f"v float[{emb['dim']}] indexed by rescore(quantizer=int8, oversample=4))"
    )
    _insert_corpus(db, "t", emb)

    recalls = []
    for (qtext, topic), qv in zip(QUERIES.items(), emb["queries"]):
        ids = _knn_ids(db, "t", qv, 10)
        recalls.append(_recall(ids, _exact_knn(emb["corpus"], qv, 10)))
        assert _topic_of(ids[0]) == topic
    assert sum(recalls) / len(recalls) >= 0.9, f"mean recall too low: {recalls}"


@needs_rescore
def test_rescore_bit_recall(db, emb):
    # bit quantizer requires dimensions divisible by 8; real models usually are
    if emb["dim"] % 8 != 0:
        pytest.skip("embedding dimension not divisible by 8")
    db.execute(
        f"CREATE VIRTUAL TABLE t USING vec0("
        f"v float[{emb['dim']}] indexed by rescore(quantizer=bit, oversample=8))"
    )
    _insert_corpus(db, "t", emb)

    recalls = [
        _recall(_knn_ids(db, "t", qv, 10), _exact_knn(emb["corpus"], qv, 10))
        for qv in emb["queries"]
    ]
    assert sum(recalls) / len(recalls) >= 0.6, f"mean recall too low: {recalls}"
