# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`sqlite-vec` is a lightweight, fast vector search SQLite extension written in pure C with no dependencies. It's a pre-v1 project (current: v0.1.7-alpha.2) that provides vector similarity search capabilities for SQLite databases across all platforms where SQLite runs.

Key features:
- Supports float, int8, and binary vector types via `vec0` virtual tables
- Pure C implementation with optional SIMD optimizations (AVX on x86_64, NEON on ARM)
- Multi-language bindings (Python, Node.js, Ruby, Go, Rust)
- Runs anywhere: Linux/MacOS/Windows, WASM, embedded devices

## Building and Testing

### Build Commands

Run `./scripts/vendor.sh` first to download vendored dependencies (sqlite3.c, shell.c).

**Core builds:**
- `make loadable` - Build `dist/vec0.{so,dylib,dll}` loadable extension
- `make static` - Build `dist/libsqlite_vec0.a` static library and `dist/sqlite-vec.h` header
- `make cli` - Build `dist/sqlite3` CLI with sqlite-vec statically linked
- `make all` - Build all three targets above
- `make wasm` - Build WASM version (requires emcc)

**Platform-specific compiler:**
- Set `CC=` to use a different compiler (default: gcc)
- Set `AR=` to use a different archiver (default: ar)

**SIMD control:**
- SIMD is auto-enabled on Darwin x86_64 (AVX) and Darwin arm64 (NEON)
- Set `OMIT_SIMD=1` to disable SIMD optimizations

### Testing

**Python tests (primary test suite):**
```bash
# Setup test environment with uv
uv sync --directory tests

# Run all Python tests
make test-loadable python=./tests/.venv/bin/python

# Run specific test
./tests/.venv/bin/python -m pytest tests/test-loadable.py::test_name -vv -s -x

# Update snapshots
make test-loadable-snapshot-update

# Watch mode
make test-loadable-watch
```

**Other tests:**
- `make test` - Run basic SQL tests via `test.sql`
- `make test-unit` - Compile and run C unit tests
- `sqlite3 :memory: '.read test.sql'` - Quick smoke test

**Test structure:**
- `tests/test-loadable.py` - Main comprehensive test suite
- `tests/test-metadata.py` - Metadata column tests
- `tests/test-auxiliary.py` - Auxiliary column tests
- `tests/test-partition-keys.py` - Partition key tests
- `tests/conftest.py` - pytest fixtures (loads extension from `dist/vec0`)

### Code Quality

- `make format` - Format C code with clang-format and Python with black
- `make lint` - Check formatting without modifying files

## Architecture

### Core Implementation (sqlite-vec.c)

The entire extension is in a single `sqlite-vec.c` file (~9000 lines). It implements a `vec0` virtual table module using SQLite's virtual table API.

**Key concepts:**

1. **vec0 virtual table**: Declared with `CREATE VIRTUAL TABLE x USING vec0(vector_column TYPE[N], ...)`
   - Vector column: Must specify type (float, int8, bit) and dimensions
   - Metadata columns: Additional indexed columns for filtering
   - Auxiliary columns: Non-indexed columns for associated data
   - Partition keys: Special columns for pre-filtering via `partition_key=column_name`
   - Chunk size: Configurable via `chunk_size=N` (default varies by type)

2. **Shadow tables**: vec0 creates multiple hidden tables to store data:
   - `xyz_chunks` - Chunk metadata (size, validity bitmaps, rowids)
   - `xyz_rowids` - Rowid mapping to chunks
   - `xyz_vector_chunksNN` - Actual vector data for column NN
   - `xyz_auxiliary` - Auxiliary column values
   - `xyz_metadatachunksNN` / `xyz_metadatatextNN` - Metadata storage

3. **Query plans**: Determined in xBestIndex, encoded in idxStr:
   - `VEC0_QUERY_PLAN_FULLSCAN` - Full table scan
   - `VEC0_QUERY_PLAN_POINT` - Single rowid lookup
   - `VEC0_QUERY_PLAN_KNN` - K-nearest neighbors vector search

See ARCHITECTURE.md for detailed idxStr encoding and shadow table schemas.

### Language Bindings

All bindings wrap the core C extension:

- **Python** (`bindings/python/`): Minimal wrapper with helper functions in `extra_init.py` for vector serialization
- **Go** (`bindings/go/`): Uses ncruces/go-sqlite3 pure Go implementation
- **Rust** (`bindings/rust/`): Static linking via build.rs, exports `sqlite3_vec_init()`

### Documentation Site

Built with VitePress (Vue-based static site generator):
- `npm --prefix site run dev` - Development server
- `npm --prefix site run build` - Production build
- Source: `site/` directory
- Deployed via GitHub Actions (`.github/workflows/site.yaml`)

## Development Workflow

### Making Changes

1. Edit `sqlite-vec.c` for core functionality
2. Update `sqlite-vec.h.tmpl` if public API changes (regenerated via `make sqlite-vec.h`)
3. Add tests to `tests/test-loadable.py` or other test files
4. Run `make format` before committing
5. Verify with `make test-loadable`

### Release Process

1. Update `VERSION` file (format: `X.Y.Z` or `X.Y.Z-alpha.N`)
2. Run `./scripts/publish-release.sh` - This:
   - Commits version changes
   - Creates git tag
   - Pushes to origin
   - Creates GitHub release (pre-release if alpha/beta)

CI/CD (`.github/workflows/release.yaml`) then builds and publishes:
- Platform-specific extensions (Linux, macOS, Windows, Android, WASM)
- Language-specific packages (PyPI, npm, crates.io, RubyGems)

### Working with Tests

**Python test fixtures:**
- `@pytest.fixture() db()` in conftest.py provides SQLite connection with extension loaded
- Tests use `db.execute()` for queries
- Snapshot testing available for regression tests

**Common test patterns:**
```python
def test_example(db):
    db.execute("CREATE VIRTUAL TABLE v USING vec0(embedding float[3])")
    db.execute("INSERT INTO v(rowid, embedding) VALUES (1, '[1,2,3]')")
    result = db.execute("SELECT distance FROM v WHERE embedding MATCH '[1,2,3]'").fetchone()
```

### SIMD Optimizations

SIMD is conditionally compiled based on platform:
- `SQLITE_VEC_ENABLE_AVX` - x86_64 AVX instructions
- `SQLITE_VEC_ENABLE_NEON` - ARM NEON instructions

Code uses preprocessor directives to select implementations. Distance calculations have both scalar and SIMD variants.

## Important Notes

- This is pre-v1 software - breaking changes are expected
- The single-file architecture means recompiling for any change
- Tests must run from repository root (assumes `dist/vec0` exists)
- All bindings depend on the core C extension being built first
- Vector format: JSON arrays `'[1,2,3]'` or raw bytes via helper functions
