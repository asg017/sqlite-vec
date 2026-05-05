MODEL ?= mixedbread-ai/mxbai-embed-large-v1
K ?= 100
BATCH_SIZE ?= 256
DATA_DIR ?= ../nyt/data

all: base.db

# Reuse data from ../nyt
$(DATA_DIR):
	$(MAKE) -C ../nyt data

contents.db: $(DATA_DIR)
	uv run ../nyt-768/build-contents.py --data-dir $(DATA_DIR) -o $@

base.db: contents.db queries.txt
	uv run build-base.py \
		--contents-db contents.db \
		--model $(MODEL) \
		--queries-file queries.txt \
		--batch-size $(BATCH_SIZE) \
		--k $(K) \
		-o $@

queries.txt:
	cp ../nyt/queries.txt $@

clean:
	rm -f base.db contents.db

.PHONY: all clean
