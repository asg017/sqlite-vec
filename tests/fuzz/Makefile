
TARGET_DIR=./targets

$(TARGET_DIR):
	mkdir -p $@

# ASAN_OPTIONS=detect_leaks=1  ./fuzz_json -detect_leaks=1 '-trace_malloc=[12]' tmp
$(TARGET_DIR)/json: json.c $(TARGET_DIR)
	/opt/homebrew/opt/llvm/bin/clang \
	-fsanitize=address,fuzzer \
	-I ../../ -I ../../vendor -DSQLITE_CORE -g \
	../../vendor/sqlite3.c \
	../../sqlite-vec.c \
	$< \
	-o $@


$(TARGET_DIR)/vec0_create: vec0-create.c ../../sqlite-vec.c $(TARGET_DIR)
	/opt/homebrew/opt/llvm/bin/clang \
	-fsanitize=address,fuzzer \
	-I ../../ -I ../../vendor -DSQLITE_CORE -g \
	../../vendor/sqlite3.c \
	../../sqlite-vec.c \
	$< \
	-o $@

$(TARGET_DIR)/numpy: numpy.c ../../sqlite-vec.c $(TARGET_DIR)
	/opt/homebrew/opt/llvm/bin/clang \
	-fsanitize=address,fuzzer \
	-I ../../ -I ../../vendor -DSQLITE_CORE -g \
	../../vendor/sqlite3.c \
	../../sqlite-vec.c \
	$< \
	-o $@

$(TARGET_DIR)/exec: exec.c ../../sqlite-vec.c $(TARGET_DIR)
	/opt/homebrew/opt/llvm/bin/clang \
	-fsanitize=address,fuzzer \
	-I ../../ -I ../../vendor -DSQLITE_CORE -g \
	../../vendor/sqlite3.c \
	../../sqlite-vec.c \
	$< \
	-o $@

all: $(TARGET_DIR)/json $(TARGET_DIR)/numpy $(TARGET_DIR)/json $(TARGET_DIR)/exec

clean:
	rm -rf $(TARGET_DIR)/*
