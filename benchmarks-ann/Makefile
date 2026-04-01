BENCH = python bench.py
BASE_DB = cohere1m/base.db
EXT = ../dist/vec0

# --- Baseline (brute-force) configs ---
BASELINES = \
	"brute-float:type=vec0-flat,variant=float" \
	"brute-int8:type=vec0-flat,variant=int8" \
	"brute-bit:type=vec0-flat,variant=bit"

# --- IVF configs ---
IVF_CONFIGS = \
	"ivf-n32-p8:type=ivf,nlist=32,nprobe=8" \
	"ivf-n128-p16:type=ivf,nlist=128,nprobe=16" \
	"ivf-n512-p32:type=ivf,nlist=512,nprobe=32"

RESCORE_CONFIGS = \
	"rescore-bit-os8:type=rescore,quantizer=bit,oversample=8" \
	"rescore-bit-os16:type=rescore,quantizer=bit,oversample=16" \
	"rescore-int8-os8:type=rescore,quantizer=int8,oversample=8"

# --- DiskANN configs ---
DISKANN_CONFIGS = \
	"diskann-R48-binary:type=diskann,R=48,L=128,quantizer=binary" \
	"diskann-R72-binary:type=diskann,R=72,L=128,quantizer=binary" \
	"diskann-R72-int8:type=diskann,R=72,L=128,quantizer=int8" \
	"diskann-R72-L256:type=diskann,R=72,L=256,quantizer=binary"

ALL_CONFIGS = $(BASELINES) $(RESCORE_CONFIGS) $(IVF_CONFIGS) $(DISKANN_CONFIGS)

.PHONY: seed ground-truth bench-smoke bench-rescore bench-ivf bench-diskann bench-10k bench-50k bench-100k bench-all \
        report clean

# --- Data preparation ---
seed:
	$(MAKE) -C cohere1m

ground-truth: seed
	python ground_truth.py --subset-size 10000
	python ground_truth.py --subset-size 50000
	python ground_truth.py --subset-size 100000

# --- Quick smoke test ---
bench-smoke: seed
	$(BENCH) --subset-size 5000 -k 10 -n 20 --dataset cohere1m -o runs \
		"brute-float:type=vec0-flat,variant=float" \
		"ivf-quick:type=ivf,nlist=16,nprobe=4" \
		"diskann-quick:type=diskann,R=48,L=64,quantizer=binary"

bench-rescore: seed
	$(BENCH) --subset-size 10000 -k 10 --dataset cohere1m -o runs \
		$(RESCORE_CONFIGS)


# --- Standard sizes ---
bench-10k: seed
	$(BENCH) --subset-size 10000 -k 10 --dataset cohere1m -o runs $(ALL_CONFIGS)

bench-50k: seed
	$(BENCH) --subset-size 50000 -k 10 --dataset cohere1m -o runs $(ALL_CONFIGS)

bench-100k: seed
	$(BENCH) --subset-size 100000 -k 10 --dataset cohere1m -o runs $(ALL_CONFIGS)

bench-all: bench-10k bench-50k bench-100k

# --- IVF across sizes ---
bench-ivf: seed
	$(BENCH) --subset-size 10000 -k 10 --dataset cohere1m -o runs $(BASELINES) $(IVF_CONFIGS)
	$(BENCH) --subset-size 50000 -k 10 --dataset cohere1m -o runs $(BASELINES) $(IVF_CONFIGS)
	$(BENCH) --subset-size 100000 -k 10 --dataset cohere1m -o runs $(BASELINES) $(IVF_CONFIGS)

# --- DiskANN across sizes ---
bench-diskann: seed
	$(BENCH) --subset-size 10000 -k 10 --dataset cohere1m -o runs $(BASELINES) $(DISKANN_CONFIGS)
	$(BENCH) --subset-size 50000 -k 10 --dataset cohere1m -o runs $(BASELINES) $(DISKANN_CONFIGS)
	$(BENCH) --subset-size 100000 -k 10 --dataset cohere1m -o runs $(BASELINES) $(DISKANN_CONFIGS)

# --- Report ---
report:
	@echo "Use: sqlite3 runs/cohere1m/<size>/results.db 'SELECT run_id, config_name, status, recall FROM runs JOIN run_results USING(run_id)'"

# --- Cleanup ---
clean:
	rm -rf runs/
