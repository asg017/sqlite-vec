BASE_URL = https://assets.zilliz.com/benchmark/cohere_large_10m

TRAIN_PARQUETS = $(shell printf 'train-%02d-of-10.parquet ' 0 1 2 3 4 5 6 7 8 9)
OTHER_PARQUETS = test.parquet neighbors.parquet
PARQUETS = $(TRAIN_PARQUETS) $(OTHER_PARQUETS)

.PHONY: all download clean

all: base.db

# Use: make -j12 download
download: $(PARQUETS)

train-%-of-10.parquet:
	curl -L -o $@ $(BASE_URL)/$@

test.parquet:
	curl -L -o $@ $(BASE_URL)/test.parquet

neighbors.parquet:
	curl -L -o $@ $(BASE_URL)/neighbors.parquet

base.db: $(PARQUETS) build_base_db.py
	uv run --with pandas --with pyarrow python build_base_db.py

clean:
	rm -f base.db
