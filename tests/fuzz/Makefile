# Auto-detect clang with libFuzzer support.
# Priority: Homebrew LLVM (macOS ARM) → Homebrew LLVM (macOS Intel) →
#           versioned clang (Linux) → system clang
FUZZ_CC ?= $(shell \
  if [ -x /opt/homebrew/opt/llvm/bin/clang ]; then \
    echo "/opt/homebrew/opt/llvm/bin/clang"; \
  elif [ -x /usr/local/opt/llvm/bin/clang ]; then \
    echo "/usr/local/opt/llvm/bin/clang"; \
  elif command -v clang-18 >/dev/null 2>&1; then \
    echo "clang-18"; \
  elif command -v clang-17 >/dev/null 2>&1; then \
    echo "clang-17"; \
  elif command -v clang >/dev/null 2>&1; then \
    echo "clang"; \
  else \
    echo "FUZZ_CC_NOT_FOUND"; \
  fi)

# AddressSanitizer + UndefinedBehaviorSanitizer + libFuzzer.
# Override FUZZ_SANITIZERS to change (e.g., drop ubsan on Windows).
FUZZ_SANITIZERS ?= -fsanitize=address,undefined,fuzzer

# On macOS, Homebrew LLVM may need -Wl,-ld_classic to work with the system linker.
FUZZ_LDFLAGS ?= $(shell \
  if [ "$$(uname -s)" = "Darwin" ]; then \
    echo "-Wl,-ld_classic"; \
  fi)

FUZZ_CFLAGS = $(FUZZ_SANITIZERS) -I ../../ -I ../../vendor -DSQLITE_CORE -g $(FUZZ_LDFLAGS)
FUZZ_SRCS = ../../vendor/sqlite3.c ../../sqlite-vec.c

TARGET_DIR = ./targets

$(TARGET_DIR):
	mkdir -p $@

# Existing targets (filename uses -, Makefile target uses _)
$(TARGET_DIR)/vec0_create: vec0-create.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/exec: exec.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/json: json.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/numpy: numpy.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

# New targets
$(TARGET_DIR)/shadow_corrupt: shadow-corrupt.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/vec0_operations: vec0-operations.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/scalar_functions: scalar-functions.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/vec0_create_full: vec0-create-full.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/metadata_columns: metadata-columns.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/vec_each: vec-each.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/vec_mismatch: vec-mismatch.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/vec0_delete_completeness: vec0-delete-completeness.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_operations: rescore-operations.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_create: rescore-create.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_quantize: rescore-quantize.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE -DSQLITE_VEC_TEST $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_shadow_corrupt: rescore-shadow-corrupt.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_knn_deep: rescore-knn-deep.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_quantize_edge: rescore-quantize-edge.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE -DSQLITE_VEC_TEST $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/rescore_interleave: rescore-interleave.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) -DSQLITE_VEC_ENABLE_RESCORE $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_create: ivf-create.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_operations: ivf-operations.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_quantize: ivf-quantize.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_kmeans: ivf-kmeans.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_shadow_corrupt: ivf-shadow-corrupt.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_knn_deep: ivf-knn-deep.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_cell_overflow: ivf-cell-overflow.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

$(TARGET_DIR)/ivf_rescore: ivf-rescore.c $(FUZZ_SRCS) | $(TARGET_DIR)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SRCS) $< -o $@

FUZZ_TARGETS = vec0_create exec json numpy \
               shadow_corrupt vec0_operations scalar_functions \
               vec0_create_full metadata_columns vec_each vec_mismatch \
               vec0_delete_completeness \
               rescore_operations rescore_create rescore_quantize \
               rescore_shadow_corrupt rescore_knn_deep \
               rescore_quantize_edge rescore_interleave \
               ivf_create ivf_operations \
               ivf_quantize ivf_kmeans ivf_shadow_corrupt \
               ivf_knn_deep ivf_cell_overflow ivf_rescore

all: $(addprefix $(TARGET_DIR)/,$(FUZZ_TARGETS))

clean:
	rm -rf $(TARGET_DIR)/*

.PHONY: all clean
