MODEL ?= minishlab/potion-base-8M
K ?= 100
BATCH_SIZE ?= 512
DATA_DIR ?= data

all: base.db contents.db

# Download NYT headlines CSVs from Kaggle (requires `kaggle` CLI + API token)
$(DATA_DIR):
	kaggle datasets download -d johnbandy/new-york-times-headlines -p $(DATA_DIR) --unzip

contents.db: $(DATA_DIR)
	uv run build-contents.py --data-dir $(DATA_DIR) -o $@

base.db: contents.db queries.txt
	uv run build-base.py \
		--contents-db contents.db \
		--model $(MODEL) \
		--queries-file queries.txt \
		--batch-size $(BATCH_SIZE) \
		--k $(K) \
		-o $@

clean:
	rm -f base.db contents.db

clean-all: clean
	rm -rf $(DATA_DIR)

.PHONY: all clean clean-all
