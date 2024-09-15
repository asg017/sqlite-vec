
COMMIT=$(shell git rev-parse HEAD)
VERSION=$(shell cat VERSION)
DATE=$(shell date +'%FT%TZ%z')

INSTALL_LIB_DIR = /usr/local/lib
INSTALL_INCLUDE_DIR = /usr/local/include
INSTALL_BIN_DIR = /usr/local/bin

ifndef CC
CC=gcc
endif
ifndef AR
AR=ar
endif

ifeq ($(shell uname -s),Darwin)
CONFIG_DARWIN=y
else ifeq ($(OS),Windows_NT)
CONFIG_WINDOWS=y
else
CONFIG_LINUX=y
endif

ifdef CONFIG_DARWIN
LOADABLE_EXTENSION=dylib
endif

ifdef CONFIG_LINUX
LOADABLE_EXTENSION=so
CFLAGS += -lm
endif

ifdef CONFIG_WINDOWS
LOADABLE_EXTENSION=dll
endif


ifdef python
PYTHON=$(python)
else
PYTHON=python3
endif

ifndef OMIT_SIMD
	ifeq ($(shell uname -sm),Darwin x86_64)
	CFLAGS += -mavx -DSQLITE_VEC_ENABLE_AVX
	endif
	ifeq ($(shell uname -sm),Darwin arm64)
	CFLAGS += -mcpu=apple-m1 -DSQLITE_VEC_ENABLE_NEON
	endif
endif

ifdef USE_BREW_SQLITE
	SQLITE_INCLUDE_PATH=-I/opt/homebrew/opt/sqlite/include
	SQLITE_LIB_PATH=-L/opt/homebrew/opt/sqlite/lib
	CFLAGS += $(SQLITE_INCLUDE_PATH) $(SQLITE_LIB_PATH)
endif

ifdef IS_MACOS_ARM
RENAME_WHEELS_ARGS=--is-macos-arm
else
RENAME_WHEELS_ARGS=
endif

prefix=dist
$(prefix):
	mkdir -p $(prefix)

TARGET_LOADABLE=$(prefix)/vec0.$(LOADABLE_EXTENSION)
TARGET_STATIC=$(prefix)/libsqlite_vec0.a
TARGET_STATIC_H=$(prefix)/sqlite-vec.h
TARGET_CLI=$(prefix)/sqlite3

loadable: $(TARGET_LOADABLE)
static: $(TARGET_STATIC)
cli: $(TARGET_CLI)

all: loadable static cli

OBJS_DIR=$(prefix)/.objs
LIBS_DIR=$(prefix)/.libs
BUILD_DIR=$(prefix)/.build

$(OBJS_DIR): $(prefix)
	mkdir -p $@

$(LIBS_DIR): $(prefix)
	mkdir -p $@

$(BUILD_DIR): $(prefix)
	mkdir -p $@


$(TARGET_LOADABLE): sqlite-vec.c sqlite-vec.h $(prefix)
	$(CC) \
		-fPIC -shared \
		-Wall -Wextra \
		-Ivendor/ \
		-O3 \
		$(CFLAGS) \
		$< -o $@

$(TARGET_STATIC): sqlite-vec.c sqlite-vec.h $(prefix) $(OBJS_DIR)
	$(CC) -Ivendor/ -Ivendor/vec $(CFLAGS) -DSQLITE_CORE -DSQLITE_VEC_STATIC \
	-O3 -c  $< -o $(OBJS_DIR)/vec.o
	$(AR) rcs $@ $(OBJS_DIR)/vec.o

$(TARGET_STATIC_H): sqlite-vec.h $(prefix)
	cp $< $@


$(OBJS_DIR)/sqlite3.o: vendor/sqlite3.c $(OBJS_DIR)
	$(CC) -c -g3 -O3 -DSQLITE_EXTRA_INIT=core_init -DSQLITE_CORE -DSQLITE_ENABLE_STMT_SCANSTATUS -DSQLITE_ENABLE_BYTECODE_VTAB -DSQLITE_ENABLE_EXPLAIN_COMMENTS -I./vendor $< -o $@

$(LIBS_DIR)/sqlite3.a: $(OBJS_DIR)/sqlite3.o $(LIBS_DIR)
	$(AR) rcs $@ $<

$(BUILD_DIR)/shell-new.c: vendor/shell.c $(BUILD_DIR)
	sed 's/\/\*extra-version-info\*\//EXTRA_TODO/g' $< > $@

$(OBJS_DIR)/shell.o: $(BUILD_DIR)/shell-new.c $(OBJS_DIR)
	$(CC) -c -g3 -O3 \
		-I./vendor \
		-DSQLITE_ENABLE_STMT_SCANSTATUS -DSQLITE_ENABLE_BYTECODE_VTAB -DSQLITE_ENABLE_EXPLAIN_COMMENTS \
		-DEXTRA_TODO="\"CUSTOMBUILD:sqlite-vec\n\"" \
		$< -o $@

$(LIBS_DIR)/shell.a: $(OBJS_DIR)/shell.o $(LIBS_DIR)
	$(AR) rcs $@ $<

$(OBJS_DIR)/sqlite-vec.o: sqlite-vec.c $(OBJS_DIR)
	$(CC) -c -g3 -Ivendor/ -I./ $(CFLAGS) $< -o $@

$(LIBS_DIR)/sqlite-vec.a: $(OBJS_DIR)/sqlite-vec.o $(LIBS_DIR)
	$(AR) rcs $@ $<


$(TARGET_CLI): sqlite-vec.h $(LIBS_DIR)/sqlite-vec.a $(LIBS_DIR)/shell.a $(LIBS_DIR)/sqlite3.a examples/sqlite3-cli/core_init.c $(prefix)
	$(CC) -g3  \
	-Ivendor/ -I./ \
	-DSQLITE_CORE \
	-DSQLITE_VEC_STATIC \
	-DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_FTS4 \
	-DSQLITE_ENABLE_STMT_SCANSTATUS -DSQLITE_ENABLE_BYTECODE_VTAB -DSQLITE_ENABLE_EXPLAIN_COMMENTS \
	-DSQLITE_EXTRA_INIT=core_init \
	$(CFLAGS) \
	-ldl -lm \
	examples/sqlite3-cli/core_init.c $(LIBS_DIR)/shell.a $(LIBS_DIR)/sqlite3.a $(LIBS_DIR)/sqlite-vec.a -o $@


sqlite-vec.h: sqlite-vec.h.tmpl VERSION
	VERSION=$(shell cat VERSION) \
	DATE=$(shell date -r VERSION +'%FT%TZ%z') \
	SOURCE=$(shell git log -n 1 --pretty=format:%H -- VERSION) \
	envsubst < $< > $@

clean:
	rm -rf dist


FORMAT_FILES=sqlite-vec.h sqlite-vec.c
format: $(FORMAT_FILES)
	clang-format -i $(FORMAT_FILES)
	black tests/test-loadable.py

lint: SHELL:=/bin/bash
lint:
	diff -u <(cat $(FORMAT_FILES)) <(clang-format $(FORMAT_FILES))

progress:
	deno run --allow-read=sqlite-vec.c scripts/progress.ts


evidence-of:
	@echo "EVIDENCE-OF: V$(shell printf "%05d" $$((RANDOM % 100000)))_$(shell printf "%05d" $$((RANDOM % 100000)))"

test:
	sqlite3 :memory: '.read test.sql'

.PHONY: version loadable static test clean gh-release evidence-of install uninstall

publish-release:
	./scripts/publish-release.sh

# -k test_vec0_update
test-loadable: loadable
	$(PYTHON) -m pytest -vv -s -x tests/test-loadable.py

test-loadable-snapshot-update: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py --snapshot-update

test-loadable-watch:
	watchexec -w sqlite-vec.c -w tests/test-loadable.py -w Makefile --clear -- make test-loadable

site-dev:
	npm --prefix site run dev

site-build:
	npm --prefix site run build

install:
	install -d $(INSTALL_LIB_DIR)
	install -d $(INSTALL_INCLUDE_DIR)
	install -m 644 sqlite-vec.h $(INSTALL_INCLUDE_DIR)
	@if [ -f $(TARGET_LOADABLE) ]; then \
		install -m 644 $(TARGET_LOADABLE) $(INSTALL_LIB_DIR); \
	fi
	@if [ -f $(TARGET_STATIC) ]; then \
		install -m 644 $(TARGET_STATIC) $(INSTALL_LIB_DIR); \
	fi
	@if [ -f $(TARGET_CLI) ]; then \
		sudo install -m 755 $(TARGET_CLI) $(INSTALL_BIN_DIR); \
	fi
	ldconfig

uninstall:
	rm -f $(INSTALL_LIB_DIR)/$(notdir $(TARGET_LOADABLE))
	rm -f $(INSTALL_LIB_DIR)/$(notdir $(TARGET_STATIC))
	rm -f $(INSTALL_LIB_DIR)/$(notdir $(TARGET_CLI))
	rm -f $(INSTALL_INCLUDE_DIR)/sqlite-vec.h
	ldconfig

# ███████████████████████████████ ANDROID SECTION ███████████████████████████████

# Define Android NDK paths (update these to your actual NDK path)
# NDK_PATH ?= /path/to/android-ndk
# SYSROOT = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/sysroot

# Define target architectures
ARCHS = arm64-v8a armeabi-v7a x86 x86_64

# Define cross-compiler toolchains
CC_aarch64 = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android21-clang
CC_arm = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64/bin/armv7a-linux-androideabi21-clang
CC_x86 = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64/bin/i686-linux-android21-clang
CC_x86_64 = $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64/bin/x86_64-linux-android21-clang

# Define output directories for each architecture
OUT_DIR_aarch64 = $(prefix)/android/arm64-v8a
OUT_DIR_arm = $(prefix)/android/armeabi-v7a
OUT_DIR_x86 = $(prefix)/android/x86
OUT_DIR_x86_64 = $(prefix)/android/x86_64

# Compilation rules for each architecture
$(OUT_DIR_aarch64):
	mkdir -p $@

$(OUT_DIR_arm):
	mkdir -p $@

$(OUT_DIR_x86):
	mkdir -p $@

$(OUT_DIR_x86_64):
	mkdir -p $@

# Set the path to sqlite3ext.h, assuming it's in vendor/sqlite3/
SQLITE_INCLUDE_PATH = -Ivendor/

# Android-specific flags (no -mcpu=apple-m1 here)
ANDROID_CFLAGS = -Ivendor/ -I./ -O3 -fPIC

# Rule for compiling for arm64-v8a
android_arm64-v8a: $(OUT_DIR_aarch64)
	$(CC_aarch64) $(CFLAGS) $(SQLITE_INCLUDE_PATH) $(ANDROID_CFLAGS) -shared sqlite-vec.c -o $(OUT_DIR_aarch64)/libsqlite_vec.so

# Rule for compiling for armeabi-v7a
android_armeabi-v7a: $(OUT_DIR_arm)
	$(CC_arm) $(CFLAGS) $(SQLITE_INCLUDE_PATH) $(ANDROID_CFLAGS) -shared sqlite-vec.c -o $(OUT_DIR_arm)/libsqlite_vec.so

# Rule for compiling for x86
android_x86: $(OUT_DIR_x86)
	$(CC_x86) $(CFLAGS) $(SQLITE_INCLUDE_PATH) $(ANDROID_CFLAGS) -shared sqlite-vec.c -o $(OUT_DIR_x86)/libsqlite_vec.so

# Rule for compiling for x86_64
android_x86_64: $(OUT_DIR_x86_64)
	$(CC_x86_64) $(CFLAGS) $(SQLITE_INCLUDE_PATH) $(ANDROID_CFLAGS) -shared sqlite-vec.c -o $(OUT_DIR_x86_64)/libsqlite_vec.so

# Rule to compile for all Android architectures
android: android_arm64-v8a android_armeabi-v7a android_x86 android_x86_64

.PHONY: android android_arm64-v8a android_armeabi-v7a android_x86 android_x86_64

# ███████████████████████████████   END ANDROID   ███████████████████████████████

# ███████████████████████████████ IOS SECTION ███████████████████████████████

MIN_IOS_VERSION = 8.0

# iOS SDK paths
IOS_SDK_PATH = $(shell xcrun --sdk iphoneos --show-sdk-path)
IOS_SIMULATOR_SDK_PATH = $(shell xcrun --sdk iphonesimulator --show-sdk-path)

# iOS cross-compiler toolchains
CC_ios_arm64 = $(shell xcrun --sdk iphoneos --find clang)
CC_ios_x86_64 = $(shell xcrun --sdk iphonesimulator --find clang)

# Output directories for iOS
OUT_DIR_ios_arm64 = $(prefix)/ios/arm64
OUT_DIR_ios_x86_64 = $(prefix)/ios/x86_64
OUT_DIR_ios_arm64_simulator = $(prefix)/ios/arm64_simulator

# iOS-specific flags
IOS_CFLAGS = -Ivendor/ -I./ -O3 -fembed-bitcode -fPIC
IOS_LDFLAGS = -Wl,-ios_version_min,$(MIN_IOS_VERSION)
IOS_ARM64_FLAGS = -target arm64-apple-ios$(MIN_IOS_VERSION) -miphoneos-version-min=$(MIN_IOS_VERSION)
IOS_ARM64_SIM_FLAGS = -target arm64-apple-ios-simulator$(MIN_IOS_VERSION) -mios-simulator-version-min=$(MIN_IOS_VERSION)
IOS_X86_64_FLAGS = -target x86_64-apple-ios-simulator$(MIN_IOS_VERSION) -mios-simulator-version-min=$(MIN_IOS_VERSION)

# Compilation rules for each iOS architecture
$(OUT_DIR_ios_arm64):
	mkdir -p $@

$(OUT_DIR_ios_x86_64):
	mkdir -p $@

$(OUT_DIR_ios_arm64_simulator):
	mkdir -p $@

# Rule for compiling for iOS arm64 (device)
ios_arm64: $(OUT_DIR_ios_arm64)
	$(CC_ios_arm64) $(CFLAGS) $(IOS_CFLAGS) $(IOS_ARM64_FLAGS) -isysroot $(IOS_SDK_PATH) -c sqlite-vec.c -o $(OUT_DIR_ios_arm64)/sqlite-vec.o
	$(CC_ios_arm64) -dynamiclib -o $(OUT_DIR_ios_arm64)/sqlitevec $(OUT_DIR_ios_arm64)/sqlite-vec.o -isysroot $(IOS_SDK_PATH) $(IOS_LDFLAGS)

# Rule for compiling for iOS x86_64 (simulator)
ios_x86_64: $(OUT_DIR_ios_x86_64)
	$(CC_ios_x86_64) $(CFLAGS) $(IOS_CFLAGS) $(IOS_X86_64_FLAGS) -isysroot $(IOS_SIMULATOR_SDK_PATH) -c sqlite-vec.c -o $(OUT_DIR_ios_x86_64)/sqlite-vec.o
	$(CC_ios_x86_64) $(IOS_X86_64_FLAGS) -dynamiclib -o $(OUT_DIR_ios_x86_64)/sqlitevec $(OUT_DIR_ios_x86_64)/sqlite-vec.o -isysroot $(IOS_SIMULATOR_SDK_PATH)

# Rule for compiling for iOS arm64 (simulator)
ios_arm64_sim: $(OUT_DIR_ios_arm64_simulator)
	$(CC_ios_arm64) $(CFLAGS) $(IOS_CFLAGS) $(IOS_ARM64_SIM_FLAGS) -isysroot $(IOS_SIMULATOR_SDK_PATH) -c sqlite-vec.c -o $(OUT_DIR_ios_arm64_simulator)/sqlite-vec.o
	$(CC_ios_arm64) -dynamiclib -o $(OUT_DIR_ios_arm64_simulator)/sqlitevec $(OUT_DIR_ios_arm64_simulator)/sqlite-vec.o -isysroot $(IOS_SIMULATOR_SDK_PATH)


# Rule to compile for all iOS architectures
ios: ios_arm64 ios_x86_64 ios_arm64_sim
	lipo -create ./dist/ios/x86_64/sqlitevec ./dist/ios/arm64_simulator/sqlitevec -output dist/ios/sim_fat/sqlitevec

.PHONY: ios ios_arm64 ios_x86_64 ios_arm64_sim

# ███████████████████████████████   END IOS   ███████████████████████████████

# ███████████████████████████████ WASM SECTION ███████████████████████████████

WASM_DIR=$(prefix)/.wasm

$(WASM_DIR): $(prefix)
	mkdir -p $@

SQLITE_WASM_VERSION=3450300
SQLITE_WASM_YEAR=2024
SQLITE_WASM_SRCZIP=$(BUILD_DIR)/sqlite-src.zip
SQLITE_WASM_COMPILED_SQLITE3C=$(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/sqlite3.c
SQLITE_WASM_COMPILED_MJS=$(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm/jswasm/sqlite3.mjs
SQLITE_WASM_COMPILED_WASM=$(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm/jswasm/sqlite3.wasm

TARGET_WASM_LIB=$(WASM_DIR)/libsqlite_vec.wasm.a
TARGET_WASM_MJS=$(WASM_DIR)/sqlite3.mjs
TARGET_WASM_WASM=$(WASM_DIR)/sqlite3.wasm
TARGET_WASM=$(TARGET_WASM_MJS) $(TARGET_WASM_WASM)

$(SQLITE_WASM_SRCZIP): $(BUILD_DIR)
	curl -o $@ https://www.sqlite.org/$(SQLITE_WASM_YEAR)/sqlite-src-$(SQLITE_WASM_VERSION).zip
	touch $@

$(SQLITE_WASM_COMPILED_SQLITE3C): $(SQLITE_WASM_SRCZIP) $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ || true
	unzip -q -o $< -d $(BUILD_DIR)
	(cd $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ && ./configure --enable-all && make sqlite3.c)
	touch $@

$(TARGET_WASM_LIB): examples/wasm/wasm.c sqlite-vec.c $(BUILD_DIR) $(WASM_DIR)
	emcc -O3  -I./ -Ivendor -DSQLITE_CORE -c examples/wasm/wasm.c -o $(BUILD_DIR)/wasm.wasm.o
	emcc -O3  -I./ -Ivendor -DSQLITE_CORE -c sqlite-vec.c -o $(BUILD_DIR)/sqlite-vec.wasm.o
	emar rcs $@ $(BUILD_DIR)/wasm.wasm.o $(BUILD_DIR)/sqlite-vec.wasm.o

$(SQLITE_WASM_COMPILED_MJS) $(SQLITE_WASM_COMPILED_WASM): $(SQLITE_WASM_COMPILED_SQLITE3C) $(TARGET_WASM_LIB)
	(cd $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm && \
		make sqlite3_wasm_extra_init.c=../../../../.wasm/libsqlite_vec.wasm.a jswasm/sqlite3.mjs jswasm/sqlite3.wasm \
	)

$(TARGET_WASM_MJS): $(SQLITE_WASM_COMPILED_MJS)
	cp $< $@

$(TARGET_WASM_WASM): $(SQLITE_WASM_COMPILED_WASM)
	cp $< $@

wasm: $(TARGET_WASM)

# ███████████████████████████████   END WASM   ███████████████████████████████
