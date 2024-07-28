# `sqlite-vec` In-memory benchmark comparisions

This repo contains a benchmarks that compares KNN queries of `sqlite-vec` to other in-process vector search tools using **brute force linear scans only**. These include:


- [Faiss IndexFlatL2](https://faiss.ai/)
- [usearch with `exact=True`](https://github.com/unum-cloud/usearch)
- [libsql vector search with `vector_distance_cos`](https://turso.tech/vector)
- [numpy](https://numpy.org/), using [this approach](https://github.com/EthanRosenthal/nn-vs-ann)
- [duckdb with `list_cosine_similarity`](https://duckdb.org/docs/sql/functions/nested.html#list_cosine_similaritylist1-list2)
- [`sentence_transformers.util.semantic_search`](https://sbert.net/docs/package_reference/util.html#sentence_transformers.util.semantic_search)
- [hnswlib BFIndex](https://github.com/nmslib/hnswlib/blob/c1b9b79af3d10c6ee7b5d0afa1ce851ae975254c/TESTING_RECALL.md?plain=1#L8)


Again **ONLY BRUTE FORCE LINEAR SCANS ARE TESTED**. This benchmark does **not** test approximate nearest neighbors (ANN) implementations. This benchmarks is extremely narrow to just testing KNN searches using brute force.

A few other caveats:

- Only brute-force linear scans, no ANN
- Only CPU is used. The only tool that does offer GPU is Faiss anyway.
- Only in-memory datasets are used. Many of these tools do support serializing and reading from disk (including `sqlite-vec`) and possibly `mmap`'ing, but this only tests in-memory datasets. Mostly because of numpy
- Queries are made one after the other, **not batched.** Some tools offer APIs to query multiple inputs at the same time, but this benchmark runs queries sequentially. This was done to emulate "server request"-style queries, but multiple users would send queries at different times, making batching more difficult. To note, `sqlite-vec` does **not** support batched queries yet.


These tests are run in Python. Vectors are provided as an in-memory numpy array, and each test converts that numpy array into whatever makes sense for the given tool. For example, `sqlite-vec` tests will read those vectors into a SQLite table. DuckDB will read them into an Array array then create a DuckDB table from that.
