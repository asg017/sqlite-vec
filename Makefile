
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
	$(CC) -Ivendor/ $(CFLAGS) -DSQLITE_CORE -DSQLITE_VEC_STATIC \
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
	VERSION_MAJOR=$$(echo $$VERSION | cut -d. -f1) \
	VERSION_MINOR=$$(echo $$VERSION | cut -d. -f2) \
	VERSION_PATCH=$$(echo $$VERSION | cut -d. -f3 | cut -d- -f1) \
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
	$(PYTHON) -m pytest -vv -s -x tests/test-*.py

test-loadable-snapshot-update: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py --snapshot-update

test-loadable-watch:
	watchexec --exts c,py,Makefile --clear -- make test-loadable

test-unit:
	$(CC) tests/test-unit.c sqlite-vec.c -I./ -Ivendor -o $(prefix)/test-unit && $(prefix)/test-unit

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
