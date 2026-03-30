#include "../sqlite-vec.h"
#include "sqlite-vec-internal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

// Tests vec0_token_next(), the low-level tokenizer that extracts the next
// token from a raw char range. Covers every token type (identifier, digit,
// brackets, plus, equals), whitespace skipping, EOF on empty/whitespace-only
// input, error on unrecognised characters, and boundary behaviour where
// identifiers and digits stop at the next non-matching character.
void test_vec0_token_next() {
  printf("Starting %s...\n", __func__);
  struct Vec0Token token;
  int rc;
  char *input;

  // Single-character tokens
  input = "+";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_PLUS);

  input = "[";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_LBRACKET);

  input = "]";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_RBRACKET);

  input = "=";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_EQ);

  // Identifier
  input = "hello";
  rc = vec0_token_next(input, input + 5, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
  assert(token.start == input);
  assert(token.end == input + 5);

  // Identifier with underscores and digits
  input = "col_1a";
  rc = vec0_token_next(input, input + 6, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
  assert(token.end - token.start == 6);

  // Digit sequence
  input = "1234";
  rc = vec0_token_next(input, input + 4, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_DIGIT);
  assert(token.start == input);
  assert(token.end == input + 4);

  // Leading whitespace is skipped
  input = "  abc";
  rc = vec0_token_next(input, input + 5, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
  assert(token.end - token.start == 3);

  // Tab/newline whitespace
  input = "\t\n\r X";
  rc = vec0_token_next(input, input + 5, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_IDENTIFIER);

  // Empty input
  input = "";
  rc = vec0_token_next(input, input, &token);
  assert(rc == VEC0_TOKEN_RESULT_EOF);

  // Only whitespace
  input = "   ";
  rc = vec0_token_next(input, input + 3, &token);
  assert(rc == VEC0_TOKEN_RESULT_EOF);

  // Unrecognized character
  input = "@";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_ERROR);

  input = "!";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_ERROR);

  // Identifier stops at bracket
  input = "foo[";
  rc = vec0_token_next(input, input + 4, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
  assert(token.end - token.start == 3);

  // Digit stops at non-digit
  input = "42abc";
  rc = vec0_token_next(input, input + 5, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_DIGIT);
  assert(token.end - token.start == 2);

  // Left paren
  input = "(";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_LPAREN);

  // Right paren
  input = ")";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_RPAREN);

  // Comma
  input = ",";
  rc = vec0_token_next(input, input + 1, &token);
  assert(rc == VEC0_TOKEN_RESULT_SOME);
  assert(token.token_type == TOKEN_TYPE_COMMA);

  printf("  All vec0_token_next tests passed.\n");
}

// Tests Vec0Scanner, the stateful wrapper around vec0_token_next() that
// tracks position and yields successive tokens. Verifies correct tokenisation
// of full sequences like "abc float[128]" and "key=value", empty input,
// whitespace-heavy input, and expressions with operators ("a+b").
void test_vec0_scanner() {
  printf("Starting %s...\n", __func__);
  struct Vec0Scanner scanner;
  struct Vec0Token token;
  int rc;

  // Scan "abc float[128]"
  {
    const char *input = "abc float[128]";
    vec0_scanner_init(&scanner, input, (int)strlen(input));

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(token.end - token.start == 3);
    assert(strncmp(token.start, "abc", 3) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(token.end - token.start == 5);
    assert(strncmp(token.start, "float", 5) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_LBRACKET);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_DIGIT);
    assert(strncmp(token.start, "128", 3) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_RBRACKET);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_EOF);
  }

  // Scan "key=value"
  {
    const char *input = "key=value";
    vec0_scanner_init(&scanner, input, (int)strlen(input));

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "key", 3) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_EQ);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "value", 5) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_EOF);
  }

  // Scan empty string
  {
    const char *input = "";
    vec0_scanner_init(&scanner, input, 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_EOF);
  }

  // Scan with lots of whitespace
  {
    const char *input = "  a   b  ";
    vec0_scanner_init(&scanner, input, (int)strlen(input));

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(token.end - token.start == 1);
    assert(*token.start == 'a');

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(token.end - token.start == 1);
    assert(*token.start == 'b');

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_EOF);
  }

  // Scan "a+b"
  {
    const char *input = "a+b";
    vec0_scanner_init(&scanner, input, (int)strlen(input));

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_PLUS);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_EOF);
  }

  // Scan "diskann(k=v, k2=v2)"
  {
    const char *input = "diskann(k=v, k2=v2)";
    vec0_scanner_init(&scanner, input, (int)strlen(input));

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "diskann", 7) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_LPAREN);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "k", 1) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_EQ);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "v", 1) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_COMMA);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "k2", 2) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_EQ);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_IDENTIFIER);
    assert(strncmp(token.start, "v2", 2) == 0);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_SOME);
    assert(token.token_type == TOKEN_TYPE_RPAREN);

    rc = vec0_scanner_next(&scanner, &token);
    assert(rc == VEC0_TOKEN_RESULT_EOF);
  }

  printf("  All vec0_scanner tests passed.\n");
}

// Tests vec0_parse_vector_column(), which parses a vec0 column definition
// string like "embedding float[768] distance_metric=cosine" into a
// VectorColumnDefinition struct. Covers all element types (float/f32, int8/i8,
// bit), column names with underscores/digits, all distance metrics (L2, L1,
// cosine), the default metric, and error cases: empty input, missing type,
// unknown type, missing dimensions, unknown metric, unknown option key, and
// distance_metric on bit columns.
void test_vec0_parse_vector_column() {
  printf("Starting %s...\n", __func__);
  struct VectorColumnDefinition col;
  int rc;

  // Basic float column
  {
    const char *input = "embedding float[768]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.name_length == 9);
    assert(strncmp(col.name, "embedding", 9) == 0);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
    assert(col.dimensions == 768);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_L2);
    sqlite3_free(col.name);
  }

  // f32 alias
  {
    const char *input = "v f32[3]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32);
    assert(col.dimensions == 3);
    sqlite3_free(col.name);
  }

  // int8 column
  {
    const char *input = "quantized int8[256]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_INT8);
    assert(col.dimensions == 256);
    assert(col.name_length == 9);
    assert(strncmp(col.name, "quantized", 9) == 0);
    sqlite3_free(col.name);
  }

  // i8 alias
  {
    const char *input = "q i8[64]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_INT8);
    assert(col.dimensions == 64);
    sqlite3_free(col.name);
  }

  // bit column
  {
    const char *input = "bvec bit[1024]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_BIT);
    assert(col.dimensions == 1024);
    sqlite3_free(col.name);
  }

  // Column name with underscores and digits
  {
    const char *input = "col_name_2 float[10]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.name_length == 10);
    assert(strncmp(col.name, "col_name_2", 10) == 0);
    sqlite3_free(col.name);
  }

  // distance_metric=cosine
  {
    const char *input = "emb float[128] distance_metric=cosine";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_COSINE);
    assert(col.dimensions == 128);
    sqlite3_free(col.name);
  }

  // distance_metric=L2 (explicit)
  {
    const char *input = "emb float[128] distance_metric=L2";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_L2);
    sqlite3_free(col.name);
  }

  // distance_metric=L1
  {
    const char *input = "emb float[128] distance_metric=l1";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_L1);
    sqlite3_free(col.name);
  }

  // SQLITE_EMPTY: empty string
  {
    const char *input = "";
    rc = vec0_parse_vector_column(input, 0, &col);
    assert(rc == SQLITE_EMPTY);
  }

  // SQLITE_EMPTY: non-vector column (text primary key)
  {
    const char *input = "document_id text primary key";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_EMPTY);
  }

  // SQLITE_EMPTY: non-vector column (partition key)
  {
    const char *input = "user_id integer partition key";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_EMPTY);
  }

  // SQLITE_EMPTY: no type (single identifier)
  {
    const char *input = "emb";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_EMPTY);
  }

  // SQLITE_EMPTY: unknown type
  {
    const char *input = "emb double[128]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_EMPTY);
  }

  // SQLITE_EMPTY: unknown type (unknowntype)
  {
    const char *input = "v unknowntype[128]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_EMPTY);
  }

  // SQLITE_EMPTY: missing brackets entirely
  {
    const char *input = "emb float";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_EMPTY);
  }

  // Error: zero dimensions
  {
    const char *input = "v float[0]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: empty brackets (no dimensions)
  {
    const char *input = "v float[]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: unknown distance metric
  {
    const char *input = "emb float[128] distance_metric=hamming";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: unknown distance metric (foo)
  {
    const char *input = "v float[128] distance_metric=foo";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: unknown option key
  {
    const char *input = "emb float[128] foobar=baz";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: distance_metric on bit type
  {
    const char *input = "emb bit[64] distance_metric=cosine";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // indexed by flat()
  {
    const char *input = "emb float[768] indexed by flat()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_FLAT);
    assert(col.dimensions == 768);
    sqlite3_free(col.name);
  }

  // indexed by flat() with distance_metric
  {
    const char *input = "emb float[768] distance_metric=cosine indexed by flat()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_FLAT);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_COSINE);
    sqlite3_free(col.name);
  }

  // indexed by flat() on int8
  {
    const char *input = "emb int8[256] indexed by flat()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_FLAT);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_INT8);
    sqlite3_free(col.name);
  }

  // indexed by flat() on bit
  {
    const char *input = "emb bit[64] indexed by flat()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_FLAT);
    assert(col.element_type == SQLITE_VEC_ELEMENT_TYPE_BIT);
    sqlite3_free(col.name);
  }

  // default index_type is FLAT
  {
    const char *input = "emb float[768]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_FLAT);
    sqlite3_free(col.name);
  }

  // Error: indexed by (missing type name)
  {
    const char *input = "emb float[768] indexed by";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: indexed by unknown()
  {
    const char *input = "emb float[768] indexed by unknown()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: indexed by flat (missing parens)
  {
    const char *input = "emb float[768] indexed by flat";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: indexed flat() (missing "by")
  {
    const char *input = "emb float[768] indexed flat()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

#if SQLITE_VEC_ENABLE_IVF
  // IVF: indexed by ivf() — defaults
  {
    const char *input = "v float[4] indexed by ivf()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.dimensions == 4);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.ivf.nlist == 128);  // default
    assert(col.ivf.nprobe == 10);  // default
    sqlite3_free(col.name);
  }

  // IVF: indexed by ivf(nlist=8) — nprobe auto-clamped to 8
  {
    const char *input = "v float[4] indexed by ivf(nlist=8)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.ivf.nlist == 8);
    assert(col.ivf.nprobe == 8);  // clamped from default 10
    sqlite3_free(col.name);
  }

  // IVF: indexed by ivf(nlist=64, nprobe=8)
  {
    const char *input = "v float[4] indexed by ivf(nlist=64, nprobe=8)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.ivf.nlist == 64);
    assert(col.ivf.nprobe == 8);
    sqlite3_free(col.name);
  }

  // IVF: with distance_metric before indexed by
  {
    const char *input = "v float[4] distance_metric=cosine indexed by ivf(nlist=16)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_COSINE);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.ivf.nlist == 16);
    sqlite3_free(col.name);
  }

  // IVF: nlist=0 (deferred)
  {
    const char *input = "v float[4] indexed by ivf(nlist=0)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.nlist == 0);
    sqlite3_free(col.name);
  }

  // IVF error: nprobe > nlist
  {
    const char *input = "v float[4] indexed by ivf(nlist=4, nprobe=10)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // IVF error: unknown key
  {
    const char *input = "v float[4] indexed by ivf(bogus=1)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // IVF error: unknown index type (hnsw not supported)
  {
    const char *input = "v float[4] indexed by hnsw()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Not IVF: no ivf config
  {
    const char *input = "v float[4]";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_FLAT);
    sqlite3_free(col.name);
  }

  // IVF: quantizer=binary
  {
    const char *input = "v float[768] indexed by ivf(nlist=128, quantizer=binary)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.ivf.nlist == 128);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_BINARY);
    assert(col.ivf.oversample == 1);
    sqlite3_free(col.name);
  }

  // IVF: quantizer=int8
  {
    const char *input = "v float[768] indexed by ivf(nlist=64, quantizer=int8)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_INT8);
    sqlite3_free(col.name);
  }

  // IVF: quantizer=none (explicit)
  {
    const char *input = "v float[768] indexed by ivf(quantizer=none)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_NONE);
    sqlite3_free(col.name);
  }

  // IVF: oversample=10 with quantizer
  {
    const char *input = "v float[768] indexed by ivf(nlist=128, quantizer=binary, oversample=10)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_BINARY);
    assert(col.ivf.oversample == 10);
    assert(col.ivf.nlist == 128);
    sqlite3_free(col.name);
  }

  // IVF: all params
  {
    const char *input = "v float[768] distance_metric=cosine indexed by ivf(nlist=256, nprobe=16, quantizer=int8, oversample=4)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_COSINE);
    assert(col.ivf.nlist == 256);
    assert(col.ivf.nprobe == 16);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_INT8);
    assert(col.ivf.oversample == 4);
    sqlite3_free(col.name);
  }

  // IVF error: oversample > 1 without quantizer
  {
    const char *input = "v float[768] indexed by ivf(oversample=10)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // IVF error: unknown quantizer value
  {
    const char *input = "v float[768] indexed by ivf(quantizer=pq)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // IVF: quantizer with defaults (nlist=128 default, nprobe=10 default)
  {
    const char *input = "v float[768] indexed by ivf(quantizer=binary, oversample=5)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.nlist == 128);
    assert(col.ivf.nprobe == 10);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_BINARY);
    assert(col.ivf.oversample == 5);
    sqlite3_free(col.name);
  }
#else
  // When IVF is disabled, parsing "ivf" should fail
  {
    const char *input = "v float[4] indexed by ivf()";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }
#endif /* SQLITE_VEC_ENABLE_IVF */

  printf("  All vec0_parse_vector_column tests passed.\n");
}

// Tests vec0_parse_partition_key_definition(), which parses a vec0 partition
// key column definition like "user_id integer partition key". Verifies correct
// parsing of integer and text partition keys, column name extraction, and
// rejection of invalid inputs: empty strings, non-partition-key definitions
// ("primary key"), and misspelled keywords.
void test_vec0_parse_partition_key_definition() {
  printf("Starting %s...\n", __func__);
  typedef struct {
    char * test;
    int expected_rc;
    const char *expected_column_name;
    int expected_column_type;
  } TestCase;

  TestCase suite[] = {
    {"user_id integer partition key", SQLITE_OK, "user_id", SQLITE_INTEGER},
    {"USER_id int partition key", SQLITE_OK, "USER_id", SQLITE_INTEGER},
    {"category text partition key", SQLITE_OK, "category", SQLITE_TEXT},

    {"", SQLITE_EMPTY, "", 0},
    {"document_id text primary key", SQLITE_EMPTY, "", 0},
    {"document_id text partition keyy", SQLITE_EMPTY, "", 0},
  };
  for(int i = 0; i < countof(suite); i++) {
    char * out_column_name;
    int out_column_name_length;
    int out_column_type;
    int rc;
    rc = vec0_parse_partition_key_definition(
      suite[i].test,
      strlen(suite[i].test),
      &out_column_name,
      &out_column_name_length,
      &out_column_type
    );
    assert(rc == suite[i].expected_rc);

    if(rc == SQLITE_OK) {
      assert(out_column_name_length == strlen(suite[i].expected_column_name));
      assert(strncmp(out_column_name, suite[i].expected_column_name, out_column_name_length) == 0);
      assert(out_column_type == suite[i].expected_column_type);
    }

    printf("  Passed: \"%s\"\n", suite[i].test);
  }
}

void test_distance_l2_sqr_float() {
  printf("Starting %s...\n", __func__);
  float d;

  // Identical vectors: distance = 0
  {
    float a[] = {1.0f, 2.0f, 3.0f};
    float b[] = {1.0f, 2.0f, 3.0f};
    d = _test_distance_l2_sqr_float(a, b, 3);
    assert(d == 0.0f);
  }

  // Orthogonal unit vectors: sqrt(1+1) = sqrt(2)
  {
    float a[] = {1.0f, 0.0f, 0.0f};
    float b[] = {0.0f, 1.0f, 0.0f};
    d = _test_distance_l2_sqr_float(a, b, 3);
    assert(fabsf(d - sqrtf(2.0f)) < 1e-6f);
  }

  // Known computation: [1,2,3] vs [4,5,6] = sqrt(9+9+9) = sqrt(27)
  {
    float a[] = {1.0f, 2.0f, 3.0f};
    float b[] = {4.0f, 5.0f, 6.0f};
    d = _test_distance_l2_sqr_float(a, b, 3);
    assert(fabsf(d - sqrtf(27.0f)) < 1e-5f);
  }

  // Single dimension: sqrt(16) = 4.0
  {
    float a[] = {3.0f};
    float b[] = {7.0f};
    d = _test_distance_l2_sqr_float(a, b, 1);
    assert(d == 4.0f);
  }

  printf("  All distance_l2_sqr_float tests passed.\n");
}

void test_distance_cosine_float() {
  printf("Starting %s...\n", __func__);
  float d;

  // Identical direction: distance = 0.0
  {
    float a[] = {1.0f, 0.0f};
    float b[] = {2.0f, 0.0f};
    d = _test_distance_cosine_float(a, b, 2);
    assert(fabsf(d - 0.0f) < 1e-6f);
  }

  // Orthogonal: distance = 1.0
  {
    float a[] = {1.0f, 0.0f};
    float b[] = {0.0f, 1.0f};
    d = _test_distance_cosine_float(a, b, 2);
    assert(fabsf(d - 1.0f) < 1e-6f);
  }

  // Opposite direction: distance = 2.0
  {
    float a[] = {1.0f, 0.0f};
    float b[] = {-1.0f, 0.0f};
    d = _test_distance_cosine_float(a, b, 2);
    assert(fabsf(d - 2.0f) < 1e-6f);
  }

  printf("  All distance_cosine_float tests passed.\n");
}

void test_distance_hamming() {
  printf("Starting %s...\n", __func__);
  float d;

  // Identical bitmaps: distance = 0
  {
    unsigned char a[] = {0xFF};
    unsigned char b[] = {0xFF};
    d = _test_distance_hamming(a, b, 8);
    assert(d == 0.0f);
  }

  // All different: distance = 8
  {
    unsigned char a[] = {0xFF};
    unsigned char b[] = {0x00};
    d = _test_distance_hamming(a, b, 8);
    assert(d == 8.0f);
  }

  // Half different: 0xFF vs 0x0F = 4 bits differ
  {
    unsigned char a[] = {0xFF};
    unsigned char b[] = {0x0F};
    d = _test_distance_hamming(a, b, 8);
    assert(d == 4.0f);
  }

  // Multi-byte: [0xFF, 0x00] vs [0x00, 0xFF] = 16 bits differ
  {
    unsigned char a[] = {0xFF, 0x00};
    unsigned char b[] = {0x00, 0xFF};
    d = _test_distance_hamming(a, b, 16);
    assert(d == 16.0f);
  }

  // Large vector (256 bits = 32 bytes) — exercises NEON path on ARM
  {
    unsigned char a[32];
    unsigned char b[32];
    memset(a, 0xFF, 32);
    memset(b, 0x00, 32);
    d = _test_distance_hamming(a, b, 256);
    assert(d == 256.0f);
  }

  // Large vector (1024 bits = 128 bytes) — exercises 64-byte NEON loop
  {
    unsigned char a[128];
    unsigned char b[128];
    memset(a, 0x00, 128);
    memset(b, 0x00, 128);
    // Set every other byte to 0xFF in a, 0x00 in b -> 8 bits per byte * 64 bytes = 512
    for (int i = 0; i < 128; i += 2) {
      a[i] = 0xFF;
    }
    d = _test_distance_hamming(a, b, 1024);
    assert(d == 512.0f);
  }

  printf("  All distance_hamming tests passed.\n");
}

#ifdef SQLITE_VEC_ENABLE_RESCORE

void test_rescore_quantize_float_to_bit() {
  printf("Starting %s...\n", __func__);
  uint8_t dst[16];

  // All positive -> all bits 1
  {
    float src[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    memset(dst, 0, sizeof(dst));
    _test_rescore_quantize_float_to_bit(src, dst, 8);
    assert(dst[0] == 0xFF);
  }

  // All negative -> all bits 0
  {
    float src[8] = {-1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f, -7.0f, -8.0f};
    memset(dst, 0xFF, sizeof(dst));
    _test_rescore_quantize_float_to_bit(src, dst, 8);
    assert(dst[0] == 0x00);
  }

  // Alternating positive/negative
  {
    float src[8] = {1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};
    _test_rescore_quantize_float_to_bit(src, dst, 8);
    // bits 0,2,4,6 set => 0b01010101 = 0x55
    assert(dst[0] == 0x55);
  }

  // Zero values -> bit is set (>= 0.0f)
  {
    float src[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    _test_rescore_quantize_float_to_bit(src, dst, 8);
    assert(dst[0] == 0xFF);
  }

  // 128 dimensions -> 16 bytes output
  {
    float src[128];
    for (int i = 0; i < 128; i++) src[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    memset(dst, 0, 16);
    _test_rescore_quantize_float_to_bit(src, dst, 128);
    // Even indices set: bits 0,2,4,6 in each byte => 0x55
    for (int i = 0; i < 16; i++) {
      assert(dst[i] == 0x55);
    }
  }

  printf("  All rescore_quantize_float_to_bit tests passed.\n");
}

void test_rescore_quantize_float_to_int8() {
  printf("Starting %s...\n", __func__);
  int8_t dst[256];

  // Uniform vector -> all zeros (range=0)
  {
    float src[8] = {5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f};
    _test_rescore_quantize_float_to_int8(src, dst, 8);
    for (int i = 0; i < 8; i++) {
#if SQLITE_VEC_ENABLE_IVF
void test_ivf_quantize_int8() {
  printf("Starting %s...\n", __func__);

  // Basic values in [-1, 1] range
  {
    float src[] = {0.0f, 1.0f, -1.0f, 0.5f};
    int8_t dst[4];
    ivf_quantize_int8(src, dst, 4);
    assert(dst[0] == 0);
    assert(dst[1] == 127);
    assert(dst[2] == -127);
    assert(dst[3] == 63);  // 0.5 * 127 = 63.5, truncated to 63
  }

  // Clamping: values beyond [-1, 1]
  {
    float src[] = {2.0f, -3.0f, 100.0f, -0.01f};
    int8_t dst[4];
    ivf_quantize_int8(src, dst, 4);
    assert(dst[0] == 127);   // clamped to 1.0
    assert(dst[1] == -127);  // clamped to -1.0
    assert(dst[2] == 127);   // clamped to 1.0
    assert(dst[3] == (int8_t)(-0.01f * 127.0f));
  }

  // Zero vector
  {
    float src[] = {0.0f, 0.0f, 0.0f, 0.0f};
    int8_t dst[4];
    ivf_quantize_int8(src, dst, 4);
    for (int i = 0; i < 4; i++) {
      assert(dst[i] == 0);
    }
  }

  // [0.0, 1.0] -> should map to [-128, 127]
  {
    float src[2] = {0.0f, 1.0f};
    _test_rescore_quantize_float_to_int8(src, dst, 2);
    assert(dst[0] == -128);
    assert(dst[1] == 127);
  }

  // [-1.0, 0.0] -> should map to [-128, 127]
  {
    float src[2] = {-1.0f, 0.0f};
    _test_rescore_quantize_float_to_int8(src, dst, 2);
    assert(dst[0] == -128);
    assert(dst[1] == 127);
  }

  // Single-element: range=0 -> 0
  {
    float src[1] = {42.0f};
    _test_rescore_quantize_float_to_int8(src, dst, 1);
    assert(dst[0] == 0);
  }

  // Verify range: all outputs in [-128, 127], min near -128, max near 127
  {
    float src[4] = {-100.0f, 0.0f, 100.0f, 50.0f};
    _test_rescore_quantize_float_to_int8(src, dst, 4);
    for (int i = 0; i < 4; i++) {
      assert(dst[i] >= -128 && dst[i] <= 127);
    }
    // Min maps to -128 (exact), max maps to ~127 (may lose 1 to float rounding)
    assert(dst[0] == -128);
    assert(dst[2] >= 126 && dst[2] <= 127);
    // Middle value (50) should be positive
    assert(dst[3] > 0);
  }

  printf("  All rescore_quantize_float_to_int8 tests passed.\n");
}

void test_rescore_quantized_byte_size() {
  printf("Starting %s...\n", __func__);

  // Bit quantizer: dims/8
  assert(_test_rescore_quantized_byte_size_bit(128) == 16);
  assert(_test_rescore_quantized_byte_size_bit(8) == 1);
  assert(_test_rescore_quantized_byte_size_bit(1024) == 128);

  // Int8 quantizer: dims
  assert(_test_rescore_quantized_byte_size_int8(128) == 128);
  assert(_test_rescore_quantized_byte_size_int8(8) == 8);
  assert(_test_rescore_quantized_byte_size_int8(1024) == 1024);

  printf("  All rescore_quantized_byte_size tests passed.\n");
}

void test_vec0_parse_vector_column_rescore() {
  // Negative zero
  {
    float src[] = {-0.0f};
    int8_t dst[1];
    ivf_quantize_int8(src, dst, 1);
    assert(dst[0] == 0);
  }

  // Single element
  {
    float src[] = {0.75f};
    int8_t dst[1];
    ivf_quantize_int8(src, dst, 1);
    assert(dst[0] == (int8_t)(0.75f * 127.0f));
  }

  // Boundary: exactly 1.0 and -1.0
  {
    float src[] = {1.0f, -1.0f};
    int8_t dst[2];
    ivf_quantize_int8(src, dst, 2);
    assert(dst[0] == 127);
    assert(dst[1] == -127);
  }

  printf("  All ivf_quantize_int8 tests passed.\n");
}

void test_ivf_quantize_binary() {
  printf("Starting %s...\n", __func__);

  // Basic sign-bit quantization: positive -> 1, negative/zero -> 0
  {
    float src[] = {1.0f, -1.0f, 0.5f, -0.5f, 0.0f, 0.1f, -0.1f, 2.0f};
    uint8_t dst[1];
    ivf_quantize_binary(src, dst, 8);
    // bit 0: 1.0 > 0 -> 1  (LSB)
    // bit 1: -1.0 -> 0
    // bit 2: 0.5 > 0 -> 1
    // bit 3: -0.5 -> 0
    // bit 4: 0.0 -> 0 (not > 0)
    // bit 5: 0.1 > 0 -> 1
    // bit 6: -0.1 -> 0
    // bit 7: 2.0 > 0 -> 1
    // Expected: bits 0,2,5,7 = 0b10100101 = 0xA5
    assert(dst[0] == 0xA5);
  }

  // All positive
  {
    float src[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    uint8_t dst[1];
    ivf_quantize_binary(src, dst, 8);
    assert(dst[0] == 0xFF);
  }

  // All negative
  {
    float src[] = {-1.0f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f, -7.0f, -8.0f};
    uint8_t dst[1];
    ivf_quantize_binary(src, dst, 8);
    assert(dst[0] == 0x00);
  }

  // All zero (zero is NOT > 0, so all bits should be 0)
  {
    float src[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    uint8_t dst[1];
    ivf_quantize_binary(src, dst, 8);
    assert(dst[0] == 0x00);
  }

  // Multi-byte: 16 dimensions -> 2 bytes
  {
    float src[16];
    for (int i = 0; i < 16; i++) src[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    uint8_t dst[2];
    ivf_quantize_binary(src, dst, 16);
    // Even indices are positive: bits 0,2,4,6 in each byte
    // byte 0: bits 0,2,4,6 = 0b01010101 = 0x55
    // byte 1: same pattern = 0x55
    assert(dst[0] == 0x55);
    assert(dst[1] == 0x55);
  }

  // Single byte, only first bit set
  {
    float src[] = {0.1f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    uint8_t dst[1];
    ivf_quantize_binary(src, dst, 8);
    assert(dst[0] == 0x01);
  }

  printf("  All ivf_quantize_binary tests passed.\n");
}

void test_ivf_config_parsing() {
  printf("Starting %s...\n", __func__);
  struct VectorColumnDefinition col;
  int rc;

  // Basic bit quantizer
  {
    const char *input = "emb float[128] indexed by rescore(quantizer=bit)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_RESCORE);
    assert(col.rescore.quantizer_type == VEC0_RESCORE_QUANTIZER_BIT);
    assert(col.rescore.oversample == 8); // default
    assert(col.dimensions == 128);
    sqlite3_free(col.name);
  }

  // Int8 quantizer
  {
    const char *input = "emb float[128] indexed by rescore(quantizer=int8)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_RESCORE);
    assert(col.rescore.quantizer_type == VEC0_RESCORE_QUANTIZER_INT8);
    sqlite3_free(col.name);
  }

  // Bit quantizer with oversample
  {
    const char *input = "emb float[128] indexed by rescore(quantizer=bit, oversample=16)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_RESCORE);
    assert(col.rescore.quantizer_type == VEC0_RESCORE_QUANTIZER_BIT);
    assert(col.rescore.oversample == 16);
    sqlite3_free(col.name);
  }

  // Error: non-float element type
  {
    const char *input = "emb int8[128] indexed by rescore(quantizer=bit)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: dims not divisible by 8 for bit quantizer
  {
    const char *input = "emb float[100] indexed by rescore(quantizer=bit)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Error: missing quantizer
  {
    const char *input = "emb float[128] indexed by rescore(oversample=8)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_ERROR);
  }

  // With distance_metric=cosine
  {
    const char *input = "emb float[128] distance_metric=cosine indexed by rescore(quantizer=int8)";
    rc = vec0_parse_vector_column(input, (int)strlen(input), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_RESCORE);
    assert(col.distance_metric == VEC0_DISTANCE_METRIC_COSINE);
    assert(col.rescore.quantizer_type == VEC0_RESCORE_QUANTIZER_INT8);
    sqlite3_free(col.name);
  }

  printf("  All vec0_parse_vector_column_rescore tests passed.\n");
}

#endif /* SQLITE_VEC_ENABLE_RESCORE */
  // Default IVF config
  {
    const char *s = "v float[4] indexed by ivf()";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.index_type == VEC0_INDEX_TYPE_IVF);
    assert(col.ivf.nlist == 128);   // default
    assert(col.ivf.nprobe == 10);   // default
    assert(col.ivf.quantizer == 0); // VEC0_IVF_QUANTIZER_NONE
    sqlite3_free(col.name);
  }

  // Custom nlist and nprobe
  {
    const char *s = "v float[4] indexed by ivf(nlist=64, nprobe=8)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.nlist == 64);
    assert(col.ivf.nprobe == 8);
    sqlite3_free(col.name);
  }

  // nlist=0 (deferred)
  {
    const char *s = "v float[4] indexed by ivf(nlist=0)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.nlist == 0);
    sqlite3_free(col.name);
  }

  // Quantizer options
  {
    const char *s = "v float[8] indexed by ivf(quantizer=int8)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_INT8);
    sqlite3_free(col.name);
  }

  {
    const char *s = "v float[8] indexed by ivf(quantizer=binary)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_BINARY);
    sqlite3_free(col.name);
  }

  // nprobe > nlist (explicit) should fail
  {
    const char *s = "v float[4] indexed by ivf(nlist=4, nprobe=10)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_ERROR);
  }

  // Unknown key
  {
    const char *s = "v float[4] indexed by ivf(bogus=1)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_ERROR);
  }

  // nlist > max (65536) should fail
  {
    const char *s = "v float[4] indexed by ivf(nlist=65537)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_ERROR);
  }

  // nlist at max boundary (65536) should succeed
  {
    const char *s = "v float[4] indexed by ivf(nlist=65536)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.nlist == 65536);
    sqlite3_free(col.name);
  }

  // oversample > 1 without quantization should fail
  {
    const char *s = "v float[4] indexed by ivf(oversample=4)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_ERROR);
  }

  // oversample with quantizer should succeed
  {
    const char *s = "v float[8] indexed by ivf(quantizer=int8, oversample=4)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.oversample == 4);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_INT8);
    sqlite3_free(col.name);
  }

  // All options combined
  {
    const char *s = "v float[8] indexed by ivf(nlist=32, nprobe=4, quantizer=int8, oversample=2)";
    rc = vec0_parse_vector_column(s, (int)strlen(s), &col);
    assert(rc == SQLITE_OK);
    assert(col.ivf.nlist == 32);
    assert(col.ivf.nprobe == 4);
    assert(col.ivf.quantizer == VEC0_IVF_QUANTIZER_INT8);
    assert(col.ivf.oversample == 2);
    sqlite3_free(col.name);
  }

  printf("  All ivf_config_parsing tests passed.\n");
}
#endif /* SQLITE_VEC_ENABLE_IVF */

int main() {
  printf("Starting unit tests...\n");
#ifdef SQLITE_VEC_ENABLE_AVX
  printf("SQLITE_VEC_ENABLE_AVX=1\n");
#endif
#ifdef SQLITE_VEC_ENABLE_NEON
  printf("SQLITE_VEC_ENABLE_NEON=1\n");
#endif
#ifdef SQLITE_VEC_ENABLE_RESCORE
  printf("SQLITE_VEC_ENABLE_RESCORE=1\n");
#endif
#if !defined(SQLITE_VEC_ENABLE_AVX) && !defined(SQLITE_VEC_ENABLE_NEON)
  printf("SIMD: none\n");
#endif
  test_vec0_token_next();
  test_vec0_scanner();
  test_vec0_parse_vector_column();
  test_vec0_parse_partition_key_definition();
  test_distance_l2_sqr_float();
  test_distance_cosine_float();
  test_distance_hamming();
#ifdef SQLITE_VEC_ENABLE_RESCORE
  test_rescore_quantize_float_to_bit();
  test_rescore_quantize_float_to_int8();
  test_rescore_quantized_byte_size();
  test_vec0_parse_vector_column_rescore();
#if SQLITE_VEC_ENABLE_IVF
  test_ivf_quantize_int8();
  test_ivf_quantize_binary();
  test_ivf_config_parsing();
#endif
  printf("All unit tests passed.\n");
}
