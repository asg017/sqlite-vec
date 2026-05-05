BASE_URL = https://assets.zilliz.com/benchmark/cohere_medium_1m

PARQUETS = train.parquet test.parquet neighbors.parquet

.PHONY: all download base.db clean

all: base.db

download: $(PARQUETS)

train.parquet:
	curl -L -o $@ $(BASE_URL)/train.parquet

test.parquet:
	curl -L -o $@ $(BASE_URL)/test.parquet

neighbors.parquet:
	curl -L -o $@ $(BASE_URL)/neighbors.parquet

base.db: $(PARQUETS) build_base_db.py
	uv run --with pandas --with pyarrow python build_base_db.py

clean:
	rm -f base.db
