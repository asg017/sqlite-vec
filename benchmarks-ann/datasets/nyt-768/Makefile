MODEL ?= bge-base-en-v1.5-768
K ?= 100
BATCH_SIZE ?= 512
DATA_DIR ?= ../nyt/data

all: base.db

# Reuse data from ../nyt
$(DATA_DIR):
	$(MAKE) -C ../nyt data

# Distill model (separate step, may take a while)
$(MODEL):
	uv run distill-model.py

contents.db: $(DATA_DIR)
	uv run build-contents.py --data-dir $(DATA_DIR) -o $@

base.db: contents.db queries.txt $(MODEL)
	uv run ../nyt/build-base.py \
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

clean-all: clean
	rm -rf $(MODEL)

.PHONY: all clean clean-all
