
COMMIT=$(shell git rev-parse HEAD)
VERSION=$(shell cat VERSION)
DATE=$(shell date +'%FT%TZ%z')


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

$(TARGET_LOADABLE): sqlite-vec.c sqlite-vec.h $(prefix)
	gcc \
		-fPIC -shared \
		-Wall -Wextra \
		-Ivendor/ \
		-O3 \
		$(CFLAGS) \
		$< -o $@

$(TARGET_STATIC): sqlite-vec.c sqlite-vec.h $(prefix)
	gcc -Ivendor/sqlite -Ivendor/vec $(CFLAGS) -DSQLITE_CORE \
	-O3 -c  $< -o $(prefix)/.objs/vec.o
	ar rcs $@ $(prefix)/.objs/vec.o

$(TARGET_STATIC_H): sqlite-vec.h $(prefix)
	cp $< $@


OBJS_DIR=$(prefix)/.objs
LIBS_DIR=$(prefix)/.libs
BUILD_DIR=$(prefix)/.build

$(OBJS_DIR): $(prefix)
	mkdir -p $@

$(LIBS_DIR): $(prefix)
	mkdir -p $@

$(BUILD_DIR): $(prefix)
	mkdir -p $@

$(OBJS_DIR)/sqlite3.o: vendor/sqlite3.c $(OBJS_DIR)
	gcc -c -g3 -O3 -DSQLITE_EXTRA_INIT=core_init -DSQLITE_CORE -DSQLITE_ENABLE_STMT_SCANSTATUS -DSQLITE_ENABLE_BYTECODE_VTAB -DSQLITE_ENABLE_EXPLAIN_COMMENTS -I./vendor $< -o $@

$(LIBS_DIR)/sqlite3.a: $(OBJS_DIR)/sqlite3.o $(LIBS_DIR)
	ar rcs $@ $<

$(BUILD_DIR)/shell-new.c: vendor/shell.c $(BUILD_DIR)
	sed 's/\/\*extra-version-info\*\//EXTRA_TODO/g' $< > $@

$(OBJS_DIR)/shell.o: $(BUILD_DIR)/shell-new.c $(OBJS_DIR)
	gcc -c -g3 -O3 \
		-DHAVE_EDITLINE=1 -I./vendor \
		-DSQLITE_ENABLE_STMT_SCANSTATUS -DSQLITE_ENABLE_BYTECODE_VTAB -DSQLITE_ENABLE_EXPLAIN_COMMENTS \
		-DEXTRA_TODO="\"CUSTOM BUILD: sqlite-vec\n\"" \
		$< -o $@

$(LIBS_DIR)/shell.a: $(OBJS_DIR)/shell.o $(LIBS_DIR)
	ar rcs $@ $<

$(OBJS_DIR)/sqlite-vec.o: sqlite-vec.c $(OBJS_DIR)
	gcc -c -g3 -I./vendor $(CFLAGS) $< -o $@

$(LIBS_DIR)/sqlite-vec.a: $(OBJS_DIR)/sqlite-vec.o $(LIBS_DIR)
	ar rcs $@ $<

$(TARGET_CLI): $(LIBS_DIR)/sqlite-vec.a $(LIBS_DIR)/shell.a $(LIBS_DIR)/sqlite3.a examples/sqlite3-cli/core_init.c $(prefix)
	gcc -g3  \
	-Ivendor/sqlite -I./ \
	-DSQLITE_CORE \
	-DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_FTS4 \
	-DSQLITE_ENABLE_STMT_SCANSTATUS -DSQLITE_ENABLE_BYTECODE_VTAB -DSQLITE_ENABLE_EXPLAIN_COMMENTS \
	-DSQLITE_EXTRA_INIT=core_init \
	$(CFLAGS) \
	-lreadline -DHAVE_EDITLINE=1 \
	-ldl -lm -lreadline \
	$(LIBS_DIR)/shell.a $(LIBS_DIR)/sqlite3.a $(LIBS_DIR)/sqlite-vec.a examples/sqlite3-cli/core_init.c -o $@


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

test:
	sqlite3 :memory: '.read test.sql'

.PHONY: version loadable static test clean gh-release \
	ruby

publish-release:
	./scripts/publish_release.sh

TARGET_WHEELS=$(prefix)/wheels
INTERMEDIATE_PYPACKAGE_EXTENSION=bindings/python/sqlite_vec/

$(TARGET_WHEELS): $(prefix)
	mkdir -p $(TARGET_WHEELS)

bindings/ruby/lib/version.rb: bindings/ruby/lib/version.rb.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@

bindings/python/sqlite_vec/version.py: bindings/python/sqlite_vec/version.py.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

bindings/datasette/datasette_sqlite_vec/version.py: bindings/datasette/datasette_sqlite_vec/version.py.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

python: $(TARGET_WHEELS) $(TARGET_LOADABLE) bindings/python/setup.py bindings/python/sqlite_vec/__init__.py scripts/rename-wheels.py
	cp $(TARGET_LOADABLE) $(INTERMEDIATE_PYPACKAGE_EXTENSION)
	rm $(TARGET_WHEELS)/*.wheel || true
	pip3 wheel bindings/python/ -w $(TARGET_WHEELS)
	python3 scripts/rename-wheels.py $(TARGET_WHEELS) $(RENAME_WHEELS_ARGS)
	echo "✅ generated python wheel"

datasette: $(TARGET_WHEELS) bindings/datasette/setup.py bindings/datasette/datasette_sqlite_vec/__init__.py
	rm $(TARGET_WHEELS)/datasette* || true
	pip3 wheel bindings/datasette/ --no-deps -w $(TARGET_WHEELS)

bindings/sqlite-utils/pyproject.toml: bindings/sqlite-utils/pyproject.toml.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

bindings/sqlite-utils/sqlite_utils_sqlite_vec/version.py: bindings/sqlite-utils/sqlite_utils_sqlite_vec/version.py.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

sqlite-utils: $(TARGET_WHEELS) bindings/sqlite-utils/pyproject.toml bindings/sqlite-utils/sqlite_utils_sqlite_vec/version.py
	python3 -m build bindings/sqlite-utils -w -o $(TARGET_WHEELS)

node: VERSION bindings/node/platform-package.README.md.tmpl bindings/node/platform-package.package.json.tmpl bindings/node/sqlite-vec/package.json.tmpl scripts/node_generate_platform_packages.sh
	scripts/node_generate_platform_packages.sh

deno: VERSION bindings/deno/deno.json.tmpl
	scripts/deno_generate_package.sh


version:
	make bindings/ruby/lib/version.rb
	make bindings/python/sqlite_vec/version.py
	make bindings/datasette/datasette_sqlite_vec/version.py
	make bindings/datasette/datasette_sqlite_vec/version.py
	make bindings/sqlite-utils/pyproject.toml bindings/sqlite-utils/sqlite_utils_sqlite_vec/version.py
	make node
	make deno

test-loadable: loadable
	$(PYTHON) -m pytest -vv -s tests/test-loadable.py

test-loadable-snapshot-update: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py --snapshot-update

test-loadable-watch:
	watchexec -w sqlite-vec.c -w tests/test-loadable.py -w Makefile --clear -- make test-loadable



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

$(SQLITE_WASM_COMPILED_SQLITE3C): $(SQLITE_WASM_SRCZIP) $(BUILD_DIR)
	unzip -q -o $< -d $(BUILD_DIR)
	(cd $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ && ./configure --enable-all && make sqlite3.c)

$(TARGET_WASM_LIB): examples/wasm/wasm.c sqlite-vec.c $(BUILD_DIR) $(WASM_DIR)
	emcc -O3  -I./ -Ivendor -DSQLITE_CORE -c examples/wasm/wasm.c -o $(BUILD_DIR)/wasm.wasm.o
	emcc -O3  -I./ -Ivendor -DSQLITE_CORE -c sqlite-vec.c -o $(BUILD_DIR)/sqlite-vec.wasm.o
	emar rcs $@ $(BUILD_DIR)/wasm.wasm.o $(BUILD_DIR)/sqlite-vec.wasm.o

$(SQLITE_WASM_COMPILED_MJS) $(SQLITE_WASM_COMPILED_WASM): $(SQLITE_WASM_COMPILED_SQLITE3C) $(TARGET_WASM_LIB)
	(cd $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm && \
		make sqlite3_wasm_extra_init.c=../../../../.wasm/libsqlite_vec.wasm.a "emcc.flags=-s EXTRA_EXPORTED_RUNTIME_METHODS=['ENV'] -s FETCH")

$(TARGET_WASM_MJS): $(SQLITE_WASM_COMPILED_MJS)
	cp $< $@

$(TARGET_WASM_WASM): $(SQLITE_WASM_COMPILED_WASM)
	cp $< $@

wasm: $(TARGET_WASM)

# ███████████████████████████████   END WASM   ███████████████████████████████


# ███████████████████████████████ SITE SECTION ███████████████████████████████

WASM_TOOLKIT_NPM_TARGZ=$(BUILD_DIR)/sqlite-wasm-toolkit-npm.tar.gz

SITE_DIR=$(prefix)/.site
TARGET_SITE=$(prefix)/.site/index.html

$(WASM_TOOLKIT_NPM_TARGZ):
	curl -o $@ -q https://registry.npmjs.org/@alex.garcia/sqlite-wasm-toolkit/-/sqlite-wasm-toolkit-0.0.1-alpha.7.tgz

$(SITE_DIR)/slim.js $(SITE_DIR)/slim.css: $(WASM_TOOLKIT_NPM_TARGZ) $(SITE_DIR)
	tar -xvzf $< -C $(SITE_DIR) --strip-components=2 package/dist/slim.js package/dist/slim.css


$(SITE_DIR):
	mkdir -p $(SITE_DIR)

# $(TARGET_WASM_MJS) $(TARGET_WASM_WASM)
$(TARGET_SITE): site/index.html $(SITE_DIR)/slim.js $(SITE_DIR)/slim.css
	cp $(TARGET_WASM_MJS) $(SITE_DIR)
	cp $(TARGET_WASM_WASM) $(SITE_DIR)
	cp $< $@
site: $(TARGET_SITE)
# ███████████████████████████████   END SITE   ███████████████████████████████
