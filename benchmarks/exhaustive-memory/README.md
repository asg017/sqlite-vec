```
python3 bench/bench.py \
  -n "sift1m" \
  -i sift/sift_base.fvecs \
  -q sift/sift_query.fvecs \
  --sample 10000 --qsample 100 \
  -k 10
```

```
python3 bench.py \
  -n "sift1m" \
  -i ../../sift/sift_base.fvecs \
  -q ../../sift/sift_query.fvecs \
  --qsample 100 \
  -k 20
```
```
python3 bench.py \
  -n "sift1m" \
  -i ../../sift/sift_base.fvecs \
  -q ../../sift/sift_query.fvecs \
  --qsample 100 \
  -x faiss,vec-scalar.4096,vec-static,vec-vec0.4096.16,vec-vec0.8192.1024,usearch,duckdb,hnswlib,numpy \
  -k 20
```



```
python bench.py -n gist -i ../../gist/gist_base.fvecs -q ../../gist/gist_query.fvecs --qsample 100 -k 20 --sample 500000 -x faiss,vec-static,vec-scalar.8192,vec-scalar.16384,vec-scalar.32768,vec-vec0.16384.64,vec-vec0.16384.128,vec-vec0.16384.256,vec-vec0.16384.512,vec-vec0.16384.1024,vec-vec0.16384.2048
```


python bench.py -n gist -i ../../gist/gist_base.fvecs -q ../../gist/gist_query.fvecs --qsample 100 -k 20 --sample 500000 -x faiss,vec-static,sentence-transformers,numpy
