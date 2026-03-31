BENCH = python bench.py
BASE_DB = seed/base.db
EXT = ../dist/vec0

# --- Baseline (brute-force) configs ---
BASELINES = \
	"brute-float:type=baseline,variant=float" \
	"brute-int8:type=baseline,variant=int8" \
	"brute-bit:type=baseline,variant=bit"

# --- Index-specific configs ---
# Each index branch should add its own configs here. Example:
#
# DISKANN_CONFIGS = \
# 	"diskann-R48-binary:type=diskann,R=48,L=128,quantizer=binary" \
# 	"diskann-R72-int8:type=diskann,R=72,L=128,quantizer=int8"
#
# IVF_CONFIGS = \
# 	"ivf-n128-p16:type=ivf,nlist=128,nprobe=16"
#
# ANNOY_CONFIGS = \
# 	"annoy-t50:type=annoy,n_trees=50"

ALL_CONFIGS = $(BASELINES)

.PHONY: seed ground-truth bench-smoke bench-10k bench-50k bench-100k bench-all \
        report clean

# --- Data preparation ---
seed:
	$(MAKE) -C seed

ground-truth: seed
	python ground_truth.py --subset-size 10000
	python ground_truth.py --subset-size 50000
	python ground_truth.py --subset-size 100000

# --- Quick smoke test ---
bench-smoke: seed
	$(BENCH) --subset-size 5000 -k 10 -n 20 -o runs/smoke \
		$(BASELINES)

# --- Standard sizes ---
bench-10k: seed
	$(BENCH) --subset-size 10000 -k 10 -o runs/10k $(ALL_CONFIGS)

bench-50k: seed
	$(BENCH) --subset-size 50000 -k 10 -o runs/50k $(ALL_CONFIGS)

bench-100k: seed
	$(BENCH) --subset-size 100000 -k 10 -o runs/100k $(ALL_CONFIGS)

bench-all: bench-10k bench-50k bench-100k

# --- Report ---
report:
	@echo "Use: sqlite3 runs/<dir>/results.db 'SELECT * FROM bench_results ORDER BY recall DESC'"

# --- Cleanup ---
clean:
	rm -rf runs/
