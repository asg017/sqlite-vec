BENCH = python bench_delete.py
EXT = ../../dist/vec0

# --- Configs to test ---
FLAT = "flat:type=vec0-flat,variant=float"
RESCORE_BIT = "rescore-bit:type=rescore,quantizer=bit,oversample=8"
RESCORE_INT8 = "rescore-int8:type=rescore,quantizer=int8,oversample=8"
DISKANN_R48 = "diskann-R48:type=diskann,R=48,L=128,quantizer=binary"
DISKANN_R72 = "diskann-R72:type=diskann,R=72,L=128,quantizer=binary"

ALL_CONFIGS = $(FLAT) $(RESCORE_BIT) $(RESCORE_INT8) $(DISKANN_R48) $(DISKANN_R72)

DELETE_PCTS = 5,10,25,50,75,90

.PHONY: smoke bench-10k bench-50k bench-all report clean

# Quick smoke test (small dataset, few queries)
smoke:
	$(BENCH) --subset-size 5000 --delete-pct 10,50 -k 10 -n 20 \
		--dataset cohere1m --ext $(EXT) \
		$(FLAT) $(DISKANN_R48)

# Standard benchmarks
bench-10k:
	$(BENCH) --subset-size 10000 --delete-pct $(DELETE_PCTS) -k 10 -n 50 \
		--dataset cohere1m --ext $(EXT) $(ALL_CONFIGS)

bench-50k:
	$(BENCH) --subset-size 50000 --delete-pct $(DELETE_PCTS) -k 10 -n 50 \
		--dataset cohere1m --ext $(EXT) $(ALL_CONFIGS)

bench-all: bench-10k bench-50k

# Query saved results
report:
	@echo "Query results:"
	@echo "  sqlite3 runs/cohere1m/10000/delete_results.db \\"
	@echo "    \"SELECT config_name, delete_pct, recall, query_mean_ms, vacuum_size_mb FROM delete_runs ORDER BY config_name, delete_pct\""

clean:
	rm -rf runs/
