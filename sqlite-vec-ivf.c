/**
 * sqlite-vec-ivf.c — IVF (Inverted File Index) for sqlite-vec.
 *
 * #include'd into sqlite-vec.c after struct definitions and before vec0_init().
 *
 * Storage: fixed-size packed blob cells (capped at IVF_CELL_MAX_VECTORS).
 * Multiple cell rows per centroid. cell_id is auto-increment rowid,
 * centroid_id is indexed for lookup. This keeps blobs small (~200KB)
 * and avoids expensive overflow page traversal on insert.
 */

#ifndef SQLITE_VEC_IVF_C
#define SQLITE_VEC_IVF_C

#ifdef SQLITE_VEC_TEST
#define IVF_STATIC
#else
#define IVF_STATIC static
#endif

// When opened standalone in an editor, pull in sqlite-vec.c so the LSP
// can resolve all types (vec0_vtab, VectorColumnDefinition, etc.).
// When #include'd from sqlite-vec.c, SQLITE_VEC_H is already defined.
#ifndef SQLITE_VEC_H
#include "sqlite-vec.c" // IWYU pragma: keep
#endif

#define VEC0_IVF_DEFAULT_NLIST  128
#define VEC0_IVF_DEFAULT_NPROBE  10
#define VEC0_IVF_MAX_NLIST    65536
#define VEC0_IVF_CELL_MAX_VECTORS 64  // ~200KB per cell at 768-dim f32
#define VEC0_IVF_UNASSIGNED_CENTROID_ID (-1)

#define VEC0_SHADOW_IVF_CENTROIDS_NAME "\"%w\".\"%w_ivf_centroids%02d\""
#define VEC0_SHADOW_IVF_CELLS_NAME     "\"%w\".\"%w_ivf_cells%02d\""
#define VEC0_SHADOW_IVF_ROWID_MAP_NAME "\"%w\".\"%w_ivf_rowid_map%02d\""
#define VEC0_SHADOW_IVF_VECTORS_NAME   "\"%w\".\"%w_ivf_vectors%02d\""

// ============================================================================
// Parser
// ============================================================================

static int vec0_parse_ivf_options(struct Vec0Scanner *scanner,
                                   struct Vec0IvfConfig *config) {
  struct Vec0Token token;
  int rc;
  config->nlist = VEC0_IVF_DEFAULT_NLIST;
  config->nprobe = -1;
  config->quantizer = VEC0_IVF_QUANTIZER_NONE;
  config->oversample = 1;
  int nprobe_explicit = 0;

  rc = vec0_scanner_next(scanner, &token);
  if (rc != VEC0_TOKEN_RESULT_SOME || token.token_type != TOKEN_TYPE_LPAREN)
    return SQLITE_ERROR;

  rc = vec0_scanner_next(scanner, &token);
  if (rc == VEC0_TOKEN_RESULT_SOME && token.token_type == TOKEN_TYPE_RPAREN) {
    config->nprobe = VEC0_IVF_DEFAULT_NPROBE;
    return SQLITE_OK;
  }

  while (1) {
    if (rc != VEC0_TOKEN_RESULT_SOME || token.token_type != TOKEN_TYPE_IDENTIFIER)
      return SQLITE_ERROR;
    char *key = token.start;
    int keyLength = token.end - token.start;

    rc = vec0_scanner_next(scanner, &token);
    if (rc != VEC0_TOKEN_RESULT_SOME || token.token_type != TOKEN_TYPE_EQ)
      return SQLITE_ERROR;

    // Read value — can be digit or identifier
    rc = vec0_scanner_next(scanner, &token);
    if (rc != VEC0_TOKEN_RESULT_SOME) return SQLITE_ERROR;
    if (token.token_type != TOKEN_TYPE_DIGIT &&
        token.token_type != TOKEN_TYPE_IDENTIFIER)
      return SQLITE_ERROR;

    char *val = token.start;
    int valLength = token.end - token.start;

    if (sqlite3_strnicmp(key, "nlist", keyLength) == 0) {
      if (token.token_type != TOKEN_TYPE_DIGIT) return SQLITE_ERROR;
      int v = atoi(val);
      if (v < 0 || v > VEC0_IVF_MAX_NLIST) return SQLITE_ERROR;
      config->nlist = v;
    } else if (sqlite3_strnicmp(key, "nprobe", keyLength) == 0) {
      if (token.token_type != TOKEN_TYPE_DIGIT) return SQLITE_ERROR;
      int v = atoi(val);
      if (v < 1 || v > VEC0_IVF_MAX_NLIST) return SQLITE_ERROR;
      config->nprobe = v;
      nprobe_explicit = 1;
    } else if (sqlite3_strnicmp(key, "quantizer", keyLength) == 0) {
      if (token.token_type != TOKEN_TYPE_IDENTIFIER) return SQLITE_ERROR;
      if (sqlite3_strnicmp(val, "none", valLength) == 0) {
        config->quantizer = VEC0_IVF_QUANTIZER_NONE;
      } else if (sqlite3_strnicmp(val, "int8", valLength) == 0) {
        config->quantizer = VEC0_IVF_QUANTIZER_INT8;
      } else if (sqlite3_strnicmp(val, "binary", valLength) == 0) {
        config->quantizer = VEC0_IVF_QUANTIZER_BINARY;
      } else {
        return SQLITE_ERROR;
      }
    } else if (sqlite3_strnicmp(key, "oversample", keyLength) == 0) {
      if (token.token_type != TOKEN_TYPE_DIGIT) return SQLITE_ERROR;
      int v = atoi(val);
      if (v < 1) return SQLITE_ERROR;
      config->oversample = v;
    } else {
      return SQLITE_ERROR;
    }

    rc = vec0_scanner_next(scanner, &token);
    if (rc != VEC0_TOKEN_RESULT_SOME) return SQLITE_ERROR;
    if (token.token_type == TOKEN_TYPE_RPAREN) break;
    if (token.token_type != TOKEN_TYPE_COMMA) return SQLITE_ERROR;
    rc = vec0_scanner_next(scanner, &token);
  }

  if (config->nprobe < 0) config->nprobe = VEC0_IVF_DEFAULT_NPROBE;
  if (config->nlist > 0 && config->nprobe > config->nlist) {
    if (nprobe_explicit) return SQLITE_ERROR;
    config->nprobe = config->nlist;
  }

  // Validation: oversample > 1 only makes sense with quantization
  if (config->oversample > 1 && config->quantizer == VEC0_IVF_QUANTIZER_NONE) {
    return SQLITE_ERROR;
  }

  return SQLITE_OK;
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * Size of a stored vector in bytes, accounting for quantization.
 */
static int ivf_vec_size(vec0_vtab *p, int col_idx) {
  int D = (int)p->vector_columns[col_idx].dimensions;
  switch (p->vector_columns[col_idx].ivf.quantizer) {
  case VEC0_IVF_QUANTIZER_INT8:    return D;
  case VEC0_IVF_QUANTIZER_BINARY:  return D / 8;
  default:                          return D * (int)sizeof(float);
  }
}

/**
 * Size of the full-precision vector in bytes (always float32).
 */
static int ivf_full_vec_size(vec0_vtab *p, int col_idx) {
  return (int)(p->vector_columns[col_idx].dimensions * sizeof(float));
}

/**
 * Quantize float32 vector to int8.
 * Uses unit normalization: clamp to [-1,1], scale to [-127,127].
 */
IVF_STATIC void ivf_quantize_int8(const float *src, int8_t *dst, int D) {
  for (int i = 0; i < D; i++) {
    float v = src[i];
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    dst[i] = (int8_t)(v * 127.0f);
  }
}

/**
 * Quantize float32 vector to binary (sign-bit quantization).
 * Each bit = 1 if src[i] > 0, else 0.
 */
IVF_STATIC void ivf_quantize_binary(const float *src, uint8_t *dst, int D) {
  memset(dst, 0, D / 8);
  for (int i = 0; i < D; i++) {
    if (src[i] > 0.0f) {
      dst[i / 8] |= (1 << (i % 8));
    }
  }
}

/**
 * Quantize a float32 vector to the target type based on config.
 * dst must be pre-allocated to ivf_vec_size() bytes.
 * If quantizer=none, copies src as-is.
 */
static void ivf_quantize(vec0_vtab *p, int col_idx,
                          const float *src, void *dst) {
  int D = (int)p->vector_columns[col_idx].dimensions;
  switch (p->vector_columns[col_idx].ivf.quantizer) {
  case VEC0_IVF_QUANTIZER_INT8:
    ivf_quantize_int8(src, (int8_t *)dst, D);
    break;
  case VEC0_IVF_QUANTIZER_BINARY:
    ivf_quantize_binary(src, (uint8_t *)dst, D);
    break;
  default:
    memcpy(dst, src, D * sizeof(float));
    break;
  }
}

// Forward declaration
static float ivf_distance(vec0_vtab *p, int col_idx, const void *a, const void *b);

/**
 * Find nearest centroid. Works with quantized or float centroids.
 * vec and centroids must be in the same representation (both quantized or both float).
 * vecSize = size of one vector in bytes.
 */
static int ivf_find_nearest_centroid(vec0_vtab *p, int col_idx,
                                      const void *vec, const void *centroids,
                                      int vecSize, int k) {
  float min_dist = FLT_MAX;
  int best = 0;
  const unsigned char *cdata = (const unsigned char *)centroids;
  for (int c = 0; c < k; c++) {
    float dist = ivf_distance(p, col_idx, vec, cdata + c * vecSize);
    if (dist < min_dist) { min_dist = dist; best = c; }
  }
  return best;
}

/**
 * Compute distance between two vectors using the column's distance_metric.
 * Dispatches to SIMD-optimized functions (NEON/AVX) via distance_*_float().
 * For float32 (non-quantized) vectors.
 */
static float ivf_distance_float(vec0_vtab *p, int col_idx,
                                 const float *a, const float *b) {
  size_t dims = p->vector_columns[col_idx].dimensions;
  switch (p->vector_columns[col_idx].distance_metric) {
  case VEC0_DISTANCE_METRIC_COSINE:
    return distance_cosine_float(a, b, &dims);
  case VEC0_DISTANCE_METRIC_L1:
    return (float)distance_l1_f32(a, b, &dims);
  case VEC0_DISTANCE_METRIC_L2:
  default:
    return distance_l2_sqr_float(a, b, &dims);
  }
}

/**
 * Compute distance between two quantized vectors.
 * For int8: uses L2 or cosine on int8.
 * For binary: uses hamming distance.
 * For none: delegates to ivf_distance_float.
 */
static float ivf_distance(vec0_vtab *p, int col_idx,
                           const void *a, const void *b) {
  size_t dims = p->vector_columns[col_idx].dimensions;
  switch (p->vector_columns[col_idx].ivf.quantizer) {
  case VEC0_IVF_QUANTIZER_INT8:
    return distance_l2_sqr_int8(a, b, &dims);
  case VEC0_IVF_QUANTIZER_BINARY:
    return distance_hamming(a, b, &dims);
  default:
    return ivf_distance_float(p, col_idx, (const float *)a, (const float *)b);
  }
}

static int ivf_ensure_stmt(vec0_vtab *p, sqlite3_stmt **pStmt, const char *fmt,
                            int col_idx) {
  if (*pStmt) return SQLITE_OK;
  char *zSql = sqlite3_mprintf(fmt, p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, pStmt, NULL);
  sqlite3_free(zSql);
  return rc;
}

static int ivf_exec(vec0_vtab *p, const char *fmt, int col_idx) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(fmt, p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc == SQLITE_OK) { sqlite3_step(stmt); sqlite3_finalize(stmt); }
  return SQLITE_OK;
}

static int ivf_is_trained(vec0_vtab *p, int col_idx) {
  if (p->ivfTrainedCache[col_idx] >= 0) return p->ivfTrainedCache[col_idx];
  sqlite3_stmt *stmt = NULL;
  int trained = 0;
  char *zSql = sqlite3_mprintf(
      "SELECT value FROM " VEC0_SHADOW_INFO_NAME " WHERE key = 'ivf_trained_%d'",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return 0;
  if (sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW)
      trained = (sqlite3_column_int(stmt, 0) == 1);
  }
  sqlite3_free(zSql);
  sqlite3_finalize(stmt);
  p->ivfTrainedCache[col_idx] = trained;
  return trained;
}

// ============================================================================
// Cell operations — fixed-size cells, multiple rows per centroid
// ============================================================================

/**
 * Create a new cell row. Returns the new cell_id (rowid) via *out_cell_id.
 */
static int ivf_cell_create(vec0_vtab *p, int col_idx, i64 centroid_id,
                            i64 *out_cell_id) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  int cap = VEC0_IVF_CELL_MAX_VECTORS;
  int vecSize = ivf_vec_size(p, col_idx);
  char *zSql = sqlite3_mprintf(
      "INSERT INTO " VEC0_SHADOW_IVF_CELLS_NAME
      " (centroid_id, n_vectors, validity, rowids, vectors) VALUES (?, 0, ?, ?, ?)",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int64(stmt, 1, centroid_id);
  sqlite3_bind_zeroblob(stmt, 2, cap / 8);
  sqlite3_bind_zeroblob(stmt, 3, cap * (int)sizeof(i64));
  sqlite3_bind_zeroblob(stmt, 4, cap * vecSize);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) return SQLITE_ERROR;
  if (out_cell_id) *out_cell_id = sqlite3_last_insert_rowid(p->db);
  return SQLITE_OK;
}

/**
 * Find a cell with space for the given centroid, or create one.
 * Returns cell_id (rowid) and current n_vectors.
 */
static int ivf_cell_find_or_create(vec0_vtab *p, int col_idx, i64 centroid_id,
                                     i64 *out_cell_id, int *out_n) {
  int rc;
  // Find existing cell with space
  rc = ivf_ensure_stmt(p, &p->stmtIvfCellMeta[col_idx],
      "SELECT rowid, n_vectors FROM " VEC0_SHADOW_IVF_CELLS_NAME
      " WHERE centroid_id = ? AND n_vectors < %d LIMIT 1",
      col_idx);
  // The %d in the format won't work with ivf_ensure_stmt since it only has 3
  // format args. Use a direct approach instead.
  sqlite3_finalize(p->stmtIvfCellMeta[col_idx]);
  p->stmtIvfCellMeta[col_idx] = NULL;

  char *zSql = sqlite3_mprintf(
      "SELECT rowid, n_vectors FROM " VEC0_SHADOW_IVF_CELLS_NAME
      " WHERE centroid_id = ? AND n_vectors < %d LIMIT 1",
      p->schemaName, p->tableName, col_idx, VEC0_IVF_CELL_MAX_VECTORS);
  if (!zSql) return SQLITE_NOMEM;
  // Cache this manually
  if (!p->stmtIvfCellMeta[col_idx]) {
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &p->stmtIvfCellMeta[col_idx], NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;
  } else {
    sqlite3_free(zSql);
  }

  sqlite3_stmt *stmt = p->stmtIvfCellMeta[col_idx];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, 1, centroid_id);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    *out_cell_id = sqlite3_column_int64(stmt, 0);
    *out_n = sqlite3_column_int(stmt, 1);
    return SQLITE_OK;
  }

  // No cell with space — create new one
  rc = ivf_cell_create(p, col_idx, centroid_id, out_cell_id);
  *out_n = 0;
  return rc;
}

/**
 * Insert vector into cell at slot = n_vectors (append).
 * Cell must have space (n_vectors < VEC0_IVF_CELL_MAX_VECTORS).
 */
static int ivf_cell_insert(vec0_vtab *p, int col_idx, i64 centroid_id,
                            i64 rowid, const void *vectorData, int vectorSize) {
  int rc;
  i64 cell_id;
  int n_vectors;

  rc = ivf_cell_find_or_create(p, col_idx, centroid_id, &cell_id, &n_vectors);
  if (rc != SQLITE_OK) return rc;

  int slot = n_vectors;
  char *cellsTable = p->shadowIvfCellsNames[col_idx];

  // Set validity bit
  sqlite3_blob *blob = NULL;
  rc = sqlite3_blob_open(p->db, p->schemaName, cellsTable, "validity",
                          cell_id, 1, &blob);
  if (rc != SQLITE_OK) return rc;
  unsigned char bx;
  sqlite3_blob_read(blob, &bx, 1, slot / 8);
  bx |= (1 << (slot % 8));
  sqlite3_blob_write(blob, &bx, 1, slot / 8);
  sqlite3_blob_close(blob);

  // Write rowid
  rc = sqlite3_blob_open(p->db, p->schemaName, cellsTable, "rowids",
                          cell_id, 1, &blob);
  if (rc == SQLITE_OK) {
    sqlite3_blob_write(blob, &rowid, sizeof(i64), slot * (int)sizeof(i64));
    sqlite3_blob_close(blob);
  }

  // Write vector
  rc = sqlite3_blob_open(p->db, p->schemaName, cellsTable, "vectors",
                          cell_id, 1, &blob);
  if (rc == SQLITE_OK) {
    sqlite3_blob_write(blob, vectorData, vectorSize, slot * vectorSize);
    sqlite3_blob_close(blob);
  }

  // Increment n_vectors (cached)
  ivf_ensure_stmt(p, &p->stmtIvfCellUpdateN[col_idx],
      "UPDATE " VEC0_SHADOW_IVF_CELLS_NAME
      " SET n_vectors = n_vectors + 1 WHERE rowid = ?", col_idx);
  if (p->stmtIvfCellUpdateN[col_idx]) {
    sqlite3_stmt *s = p->stmtIvfCellUpdateN[col_idx];
    sqlite3_reset(s);
    sqlite3_bind_int64(s, 1, cell_id);
    sqlite3_step(s);
  }

  // Insert rowid_map (cached)
  ivf_ensure_stmt(p, &p->stmtIvfRowidMapInsert[col_idx],
      "INSERT INTO " VEC0_SHADOW_IVF_ROWID_MAP_NAME
      " (rowid, cell_id, slot) VALUES (?, ?, ?)", col_idx);
  if (p->stmtIvfRowidMapInsert[col_idx]) {
    sqlite3_stmt *s = p->stmtIvfRowidMapInsert[col_idx];
    sqlite3_reset(s);
    sqlite3_bind_int64(s, 1, rowid);
    sqlite3_bind_int64(s, 2, cell_id);
    sqlite3_bind_int(s, 3, slot);
    sqlite3_step(s);
  }

  return SQLITE_OK;
}

// ============================================================================
// Shadow tables
// ============================================================================

static int ivf_create_shadow_tables(vec0_vtab *p, int col_idx) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  char *zSql;

  zSql = sqlite3_mprintf(
      "CREATE TABLE " VEC0_SHADOW_IVF_CENTROIDS_NAME
      " (centroid_id INTEGER PRIMARY KEY, centroid BLOB NOT NULL)",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return SQLITE_ERROR; }
  sqlite3_finalize(stmt);

  // cell_id is rowid (auto-increment), centroid_id is indexed
  zSql = sqlite3_mprintf(
      "CREATE TABLE " VEC0_SHADOW_IVF_CELLS_NAME
      " (centroid_id INTEGER NOT NULL,"
      "  n_vectors INTEGER NOT NULL DEFAULT 0,"
      "  validity BLOB NOT NULL,"
      "  rowids BLOB NOT NULL,"
      "  vectors BLOB NOT NULL)",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return SQLITE_ERROR; }
  sqlite3_finalize(stmt);

  // Index on centroid_id for cell lookup
  zSql = sqlite3_mprintf(
      "CREATE INDEX \"%w_ivf_cells%02d_centroid\" ON \"%w_ivf_cells%02d\" (centroid_id)",
      p->tableName, col_idx, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return SQLITE_ERROR; }
  sqlite3_finalize(stmt);

  zSql = sqlite3_mprintf(
      "CREATE TABLE " VEC0_SHADOW_IVF_ROWID_MAP_NAME
      " (rowid INTEGER PRIMARY KEY, cell_id INTEGER NOT NULL, slot INTEGER NOT NULL)",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return SQLITE_ERROR; }
  sqlite3_finalize(stmt);

  // _ivf_vectors — full-precision KV store (only when quantizer != none)
  if (p->vector_columns[col_idx].ivf.quantizer != VEC0_IVF_QUANTIZER_NONE) {
    zSql = sqlite3_mprintf(
        "CREATE TABLE " VEC0_SHADOW_IVF_VECTORS_NAME
        " (rowid INTEGER PRIMARY KEY, vector BLOB NOT NULL)",
        p->schemaName, p->tableName, col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return SQLITE_ERROR; }
    sqlite3_finalize(stmt);
  }

  zSql = sqlite3_mprintf(
      "INSERT INTO " VEC0_SHADOW_INFO_NAME " (key, value) VALUES ('ivf_trained_%d', '0')",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); return SQLITE_ERROR; }
  sqlite3_finalize(stmt);

  return SQLITE_OK;
}

static int ivf_drop_shadow_tables(vec0_vtab *p, int col_idx) {
  ivf_exec(p, "DROP TABLE IF EXISTS " VEC0_SHADOW_IVF_CENTROIDS_NAME, col_idx);
  ivf_exec(p, "DROP TABLE IF EXISTS " VEC0_SHADOW_IVF_CELLS_NAME, col_idx);
  ivf_exec(p, "DROP TABLE IF EXISTS " VEC0_SHADOW_IVF_ROWID_MAP_NAME, col_idx);
  ivf_exec(p, "DROP TABLE IF EXISTS " VEC0_SHADOW_IVF_VECTORS_NAME, col_idx);
  return SQLITE_OK;
}

// ============================================================================
// Insert / Delete
// ============================================================================

static int ivf_insert(vec0_vtab *p, int col_idx, i64 rowid,
                       const void *vectorData, int vectorSize) {
  UNUSED_PARAMETER(vectorSize);
  int quantizer = p->vector_columns[col_idx].ivf.quantizer;
  int qvecSize = ivf_vec_size(p, col_idx);
  int rc;

  // Quantize the input vector (or copy as-is if no quantization)
  void *qvec = sqlite3_malloc(qvecSize);
  if (!qvec) return SQLITE_NOMEM;
  ivf_quantize(p, col_idx, (const float *)vectorData, qvec);

  if (!ivf_is_trained(p, col_idx)) {
    rc = ivf_cell_insert(p, col_idx, VEC0_IVF_UNASSIGNED_CENTROID_ID,
                          rowid, qvec, qvecSize);
  } else {
    // Find nearest centroid using quantized distance
    int best_centroid = -1;
    float min_dist = FLT_MAX;

    rc = ivf_ensure_stmt(p, &p->stmtIvfCentroidsAll[col_idx],
        "SELECT centroid_id, centroid FROM " VEC0_SHADOW_IVF_CENTROIDS_NAME, col_idx);
    if (rc != SQLITE_OK) { sqlite3_free(qvec); return rc; }
    sqlite3_stmt *stmt = p->stmtIvfCentroidsAll[col_idx];
    sqlite3_reset(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int cid = sqlite3_column_int(stmt, 0);
      const void *c = sqlite3_column_blob(stmt, 1);
      int cBytes = sqlite3_column_bytes(stmt, 1);
      if (!c || cBytes != qvecSize) continue;
      float dist = ivf_distance(p, col_idx, qvec, c);
      if (dist < min_dist) { min_dist = dist; best_centroid = cid; }
    }
    if (best_centroid < 0) { sqlite3_free(qvec); return SQLITE_ERROR; }

    rc = ivf_cell_insert(p, col_idx, best_centroid, rowid, qvec, qvecSize);
  }

  sqlite3_free(qvec);
  if (rc != SQLITE_OK) return rc;

  // Store full-precision vector in KV table when quantized
  if (quantizer != VEC0_IVF_QUANTIZER_NONE) {
    sqlite3_stmt *stmt = NULL;
    char *zSql = sqlite3_mprintf(
        "INSERT INTO " VEC0_SHADOW_IVF_VECTORS_NAME " (rowid, vector) VALUES (?, ?)",
        p->schemaName, p->tableName, col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_int64(stmt, 1, rowid);
    sqlite3_bind_blob(stmt, 2, vectorData, ivf_full_vec_size(p, col_idx), SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return SQLITE_ERROR;
  }

  return SQLITE_OK;
}

static int ivf_delete(vec0_vtab *p, int col_idx, i64 rowid) {
  int rc;
  i64 cell_id = 0;
  int slot = -1;

  rc = ivf_ensure_stmt(p, &p->stmtIvfRowidMapLookup[col_idx],
      "SELECT cell_id, slot FROM " VEC0_SHADOW_IVF_ROWID_MAP_NAME
      " WHERE rowid = ?", col_idx);
  if (rc != SQLITE_OK) return rc;
  sqlite3_stmt *s = p->stmtIvfRowidMapLookup[col_idx];
  sqlite3_reset(s);
  sqlite3_bind_int64(s, 1, rowid);
  if (sqlite3_step(s) == SQLITE_ROW) {
    cell_id = sqlite3_column_int64(s, 0);
    slot = sqlite3_column_int(s, 1);
  }
  if (slot < 0) return SQLITE_OK;

  // Clear validity bit
  char *cellsTable = p->shadowIvfCellsNames[col_idx];
  sqlite3_blob *blob = NULL;
  rc = sqlite3_blob_open(p->db, p->schemaName, cellsTable, "validity",
                          cell_id, 1, &blob);
  if (rc == SQLITE_OK) {
    unsigned char bx;
    sqlite3_blob_read(blob, &bx, 1, slot / 8);
    bx &= ~(1 << (slot % 8));
    sqlite3_blob_write(blob, &bx, 1, slot / 8);
    sqlite3_blob_close(blob);
  }

  // Decrement n_vectors
  if (p->stmtIvfCellUpdateN[col_idx]) {
    // This stmt does +1, but we want -1. Use a different cached stmt.
  }
  // Just use inline for decrement (not hot path)
  {
    sqlite3_stmt *stmtDec = NULL;
    char *zSql = sqlite3_mprintf(
        "UPDATE " VEC0_SHADOW_IVF_CELLS_NAME
        " SET n_vectors = n_vectors - 1 WHERE rowid = ?",
        p->schemaName, p->tableName, col_idx);
    if (zSql) {
      sqlite3_prepare_v2(p->db, zSql, -1, &stmtDec, NULL); sqlite3_free(zSql);
      if (stmtDec) { sqlite3_bind_int64(stmtDec, 1, cell_id); sqlite3_step(stmtDec); sqlite3_finalize(stmtDec); }
    }
  }

  // Delete from rowid_map
  ivf_ensure_stmt(p, &p->stmtIvfRowidMapDelete[col_idx],
      "DELETE FROM " VEC0_SHADOW_IVF_ROWID_MAP_NAME " WHERE rowid = ?", col_idx);
  if (p->stmtIvfRowidMapDelete[col_idx]) {
    sqlite3_stmt *sd = p->stmtIvfRowidMapDelete[col_idx];
    sqlite3_reset(sd);
    sqlite3_bind_int64(sd, 1, rowid);
    sqlite3_step(sd);
  }

  // Delete from _ivf_vectors (full-precision KV) when quantized
  if (p->vector_columns[col_idx].ivf.quantizer != VEC0_IVF_QUANTIZER_NONE) {
    sqlite3_stmt *stmtDelVec = NULL;
    char *zSql = sqlite3_mprintf(
        "DELETE FROM " VEC0_SHADOW_IVF_VECTORS_NAME " WHERE rowid = ?",
        p->schemaName, p->tableName, col_idx);
    if (zSql) {
      sqlite3_prepare_v2(p->db, zSql, -1, &stmtDelVec, NULL); sqlite3_free(zSql);
      if (stmtDelVec) { sqlite3_bind_int64(stmtDelVec, 1, rowid); sqlite3_step(stmtDelVec); sqlite3_finalize(stmtDelVec); }
    }
  }

  return SQLITE_OK;
}

// ============================================================================
// Point query
// ============================================================================

static int ivf_get_vector_data(vec0_vtab *p, i64 rowid, int col_idx,
                                void **outVector, int *outVectorSize) {
  int rc;
  int vecSize = ivf_vec_size(p, col_idx);
  i64 cell_id = 0;
  int slot = -1;

  rc = ivf_ensure_stmt(p, &p->stmtIvfRowidMapLookup[col_idx],
      "SELECT cell_id, slot FROM " VEC0_SHADOW_IVF_ROWID_MAP_NAME
      " WHERE rowid = ?", col_idx);
  if (rc != SQLITE_OK) return rc;
  sqlite3_stmt *s = p->stmtIvfRowidMapLookup[col_idx];
  sqlite3_reset(s);
  sqlite3_bind_int64(s, 1, rowid);
  if (sqlite3_step(s) != SQLITE_ROW) return SQLITE_EMPTY;
  cell_id = sqlite3_column_int64(s, 0);
  slot = sqlite3_column_int(s, 1);

  void *buf = sqlite3_malloc(vecSize);
  if (!buf) return SQLITE_NOMEM;

  sqlite3_blob *blob = NULL;
  rc = sqlite3_blob_open(p->db, p->schemaName, p->shadowIvfCellsNames[col_idx],
                          "vectors", cell_id, 0, &blob);
  if (rc != SQLITE_OK) { sqlite3_free(buf); return rc; }
  rc = sqlite3_blob_read(blob, buf, vecSize, slot * vecSize);
  sqlite3_blob_close(blob);
  if (rc != SQLITE_OK) { sqlite3_free(buf); return rc; }

  *outVector = buf;
  if (outVectorSize) *outVectorSize = vecSize;
  return SQLITE_OK;
}

// ============================================================================
// Centroid commands
// ============================================================================

static int ivf_load_all_vectors(vec0_vtab *p, int col_idx,
                                 float **out_vectors, i64 **out_rowids, int *out_N) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  int D = (int)p->vector_columns[col_idx].dimensions;
  int vecSize = D * (int)sizeof(float);
  int quantizer = p->vector_columns[col_idx].ivf.quantizer;

  // When quantized, load full-precision vectors from _ivf_vectors KV table
  if (quantizer != VEC0_IVF_QUANTIZER_NONE) {
    int total = 0;
    char *zSql = sqlite3_mprintf(
        "SELECT count(*) FROM " VEC0_SHADOW_IVF_VECTORS_NAME,
        p->schemaName, p->tableName, col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (total == 0) { *out_vectors = NULL; *out_rowids = NULL; *out_N = 0; return SQLITE_OK; }

    float *vectors = sqlite3_malloc64((i64)total * D * sizeof(float));
    i64 *rowids = sqlite3_malloc64((i64)total * sizeof(i64));
    if (!vectors || !rowids) { sqlite3_free(vectors); sqlite3_free(rowids); return SQLITE_NOMEM; }

    int idx = 0;
    zSql = sqlite3_mprintf(
        "SELECT rowid, vector FROM " VEC0_SHADOW_IVF_VECTORS_NAME,
        p->schemaName, p->tableName, col_idx);
    if (!zSql) { sqlite3_free(vectors); sqlite3_free(rowids); return SQLITE_NOMEM; }
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    if (rc == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW && idx < total) {
        rowids[idx] = sqlite3_column_int64(stmt, 0);
        const void *blob = sqlite3_column_blob(stmt, 1);
        int blobBytes = sqlite3_column_bytes(stmt, 1);
        if (blob && blobBytes == vecSize) {
          memcpy(&vectors[idx * D], blob, vecSize);
          idx++;
        }
      }
    }
    sqlite3_finalize(stmt);
    *out_vectors = vectors; *out_rowids = rowids; *out_N = idx;
    return SQLITE_OK;
  }

  // Non-quantized: load from cells (existing path)

  // Count total
  int total = 0;
  char *zSql = sqlite3_mprintf(
      "SELECT COALESCE(SUM(n_vectors),0) FROM " VEC0_SHADOW_IVF_CELLS_NAME,
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (total == 0) { *out_vectors = NULL; *out_rowids = NULL; *out_N = 0; return SQLITE_OK; }

  float *vectors = sqlite3_malloc64((i64)total * D * sizeof(float));
  i64 *rowids = sqlite3_malloc64((i64)total * sizeof(i64));
  if (!vectors || !rowids) { sqlite3_free(vectors); sqlite3_free(rowids); return SQLITE_NOMEM; }

  int idx = 0;
  zSql = sqlite3_mprintf(
      "SELECT n_vectors, validity, rowids, vectors FROM " VEC0_SHADOW_IVF_CELLS_NAME,
      p->schemaName, p->tableName, col_idx);
  if (!zSql) { sqlite3_free(vectors); sqlite3_free(rowids); return SQLITE_NOMEM; }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK) { sqlite3_free(vectors); sqlite3_free(rowids); return rc; }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int n = sqlite3_column_int(stmt, 0);
    if (n == 0) continue;
    const unsigned char *val = (const unsigned char *)sqlite3_column_blob(stmt, 1);
    const i64 *rids = (const i64 *)sqlite3_column_blob(stmt, 2);
    const float *vecs = (const float *)sqlite3_column_blob(stmt, 3);
    int valBytes = sqlite3_column_bytes(stmt, 1);
    int ridsBytes = sqlite3_column_bytes(stmt, 2);
    int vecsBytes = sqlite3_column_bytes(stmt, 3);
    if (!val || !rids || !vecs) continue;
    int cap = valBytes * 8;
    // Clamp cap to the number of entries actually backed by the rowids and vectors blobs
    if (ridsBytes / (int)sizeof(i64) < cap) cap = ridsBytes / (int)sizeof(i64);
    if (vecsBytes / vecSize < cap) cap = vecsBytes / vecSize;
    for (int i = 0; i < cap && idx < total; i++) {
      if (val[i / 8] & (1 << (i % 8))) {
        rowids[idx] = rids[i];
        memcpy(&vectors[idx * D], &vecs[i * D], vecSize);
        idx++;
      }
    }
  }
  sqlite3_finalize(stmt);
  *out_vectors = vectors; *out_rowids = rowids; *out_N = idx;
  return SQLITE_OK;
}

static void ivf_invalidate_cached(vec0_vtab *p, int col_idx) {
  sqlite3_finalize(p->stmtIvfCellMeta[col_idx]); p->stmtIvfCellMeta[col_idx] = NULL;
  sqlite3_finalize(p->stmtIvfCentroidsAll[col_idx]); p->stmtIvfCentroidsAll[col_idx] = NULL;
  sqlite3_finalize(p->stmtIvfCellUpdateN[col_idx]); p->stmtIvfCellUpdateN[col_idx] = NULL;
  sqlite3_finalize(p->stmtIvfRowidMapInsert[col_idx]); p->stmtIvfRowidMapInsert[col_idx] = NULL;
}

static int ivf_cmd_compute_centroids(vec0_vtab *p, int col_idx, int nlist_override,
                                      int max_iter, uint32_t seed) {
  int rc;
  int D = (int)p->vector_columns[col_idx].dimensions;
  int vecSize = D * (int)sizeof(float);
  int quantizer = p->vector_columns[col_idx].ivf.quantizer;
  int nlist = nlist_override > 0 ? nlist_override : p->vector_columns[col_idx].ivf.nlist;
  if (nlist <= 0) { vtab_set_error(&p->base, "nlist must be specified"); return SQLITE_ERROR; }

  float *vectors = NULL; i64 *rowids = NULL; int N = 0;
  rc = ivf_load_all_vectors(p, col_idx, &vectors, &rowids, &N);
  if (rc != SQLITE_OK) return rc;
  if (N == 0) { vtab_set_error(&p->base, "No vectors"); sqlite3_free(vectors); sqlite3_free(rowids); return SQLITE_ERROR; }
  if (nlist > N) nlist = N;

  float *centroids = sqlite3_malloc64((i64)nlist * D * sizeof(float));
  if (!centroids) { sqlite3_free(vectors); sqlite3_free(rowids); return SQLITE_NOMEM; }
  if (ivf_kmeans(vectors, N, D, nlist, max_iter, seed, centroids) != 0) {
    sqlite3_free(vectors); sqlite3_free(rowids); sqlite3_free(centroids); return SQLITE_ERROR;
  }

  // Compute assignments
  int *assignments = sqlite3_malloc64((i64)N * sizeof(int));
  if (!assignments) { sqlite3_free(vectors); sqlite3_free(rowids); sqlite3_free(centroids); return SQLITE_NOMEM; }
  // Assignment uses float32 distances (k-means operates in float32 space)
  for (int i = 0; i < N; i++) {
    float min_d = FLT_MAX;
    int best = 0;
    for (int c = 0; c < nlist; c++) {
      float d = ivf_distance_float(p, col_idx, &vectors[i * D], &centroids[c * D]);
      if (d < min_d) { min_d = d; best = c; }
    }
    assignments[i] = best;
  }

  // Invalidate all cached stmts before dropping/recreating tables
  ivf_invalidate_cached(p, col_idx);

  sqlite3_exec(p->db, "SAVEPOINT ivf_train", NULL, NULL, NULL);
  sqlite3_stmt *stmt = NULL;
  char *zSql;

  // Clear all data
  ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_CENTROIDS_NAME, col_idx);
  ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_CELLS_NAME, col_idx);
  ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_ROWID_MAP_NAME, col_idx);

  // Write centroids (quantized if quantizer is set)
  int qvecSize = ivf_vec_size(p, col_idx);
  void *qbuf = sqlite3_malloc(qvecSize > vecSize ? qvecSize : vecSize);
  if (!qbuf) { rc = SQLITE_NOMEM; goto train_error; }

  zSql = sqlite3_mprintf(
      "INSERT INTO " VEC0_SHADOW_IVF_CENTROIDS_NAME " (centroid_id, centroid) VALUES (?, ?)",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) { sqlite3_free(qbuf); rc = SQLITE_NOMEM; goto train_error; }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK) { sqlite3_free(qbuf); goto train_error; }
  for (int i = 0; i < nlist; i++) {
    ivf_quantize(p, col_idx, &centroids[i * D], qbuf);
    sqlite3_reset(stmt);
    sqlite3_bind_int(stmt, 1, i);
    sqlite3_bind_blob(stmt, 2, qbuf, qvecSize, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) { sqlite3_finalize(stmt); sqlite3_free(qbuf); rc = SQLITE_ERROR; goto train_error; }
  }
  sqlite3_finalize(stmt);

  // Build cells: group vectors by centroid, create fixed-size cells
  {
    // Prepare INSERT statements
    sqlite3_stmt *stmtCell = NULL;
    zSql = sqlite3_mprintf(
        "INSERT INTO " VEC0_SHADOW_IVF_CELLS_NAME
        " (centroid_id, n_vectors, validity, rowids, vectors) VALUES (?, ?, ?, ?, ?)",
        p->schemaName, p->tableName, col_idx);
    if (!zSql) { rc = SQLITE_NOMEM; goto train_error; }
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtCell, NULL); sqlite3_free(zSql);
    if (rc != SQLITE_OK) goto train_error;

    sqlite3_stmt *stmtMap = NULL;
    zSql = sqlite3_mprintf(
        "INSERT INTO " VEC0_SHADOW_IVF_ROWID_MAP_NAME " (rowid, cell_id, slot) VALUES (?, ?, ?)",
        p->schemaName, p->tableName, col_idx);
    if (!zSql) { sqlite3_finalize(stmtCell); rc = SQLITE_NOMEM; goto train_error; }
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtMap, NULL); sqlite3_free(zSql);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmtCell); goto train_error; }

    int cap = VEC0_IVF_CELL_MAX_VECTORS;
    unsigned char *val = sqlite3_malloc(cap / 8);
    i64 *rids = sqlite3_malloc64((i64)cap * sizeof(i64));
    unsigned char *vecs = sqlite3_malloc64((i64)cap * qvecSize);  // quantized size
    if (!val || !rids || !vecs) {
      sqlite3_free(val); sqlite3_free(rids); sqlite3_free(vecs);
      sqlite3_finalize(stmtCell); sqlite3_finalize(stmtMap);
      sqlite3_free(qbuf);
      rc = SQLITE_NOMEM; goto train_error;
    }

    // Process one centroid at a time
    for (int c = 0; c < nlist; c++) {
      int slot = 0;
      memset(val, 0, cap / 8);
      memset(rids, 0, cap * sizeof(i64));

      for (int i = 0; i < N; i++) {
        if (assignments[i] != c) continue;

        if (slot >= cap) {
          // Flush current cell
          sqlite3_reset(stmtCell);
          sqlite3_bind_int(stmtCell, 1, c);
          sqlite3_bind_int(stmtCell, 2, slot);
          sqlite3_bind_blob(stmtCell, 3, val, cap / 8, SQLITE_TRANSIENT);
          sqlite3_bind_blob(stmtCell, 4, rids, cap * (int)sizeof(i64), SQLITE_TRANSIENT);
          sqlite3_bind_blob(stmtCell, 5, vecs, cap * qvecSize, SQLITE_TRANSIENT);
          sqlite3_step(stmtCell);
          i64 flushed_cell_id = sqlite3_last_insert_rowid(p->db);

          for (int s = 0; s < slot; s++) {
            sqlite3_reset(stmtMap);
            sqlite3_bind_int64(stmtMap, 1, rids[s]);
            sqlite3_bind_int64(stmtMap, 2, flushed_cell_id);
            sqlite3_bind_int(stmtMap, 3, s);
            sqlite3_step(stmtMap);
          }

          slot = 0;
          memset(val, 0, cap / 8);
          memset(rids, 0, cap * sizeof(i64));
        }

        val[slot / 8] |= (1 << (slot % 8));
        rids[slot] = rowids[i];
        // Quantize float32 vector into cell blob
        ivf_quantize(p, col_idx, &vectors[i * D], &vecs[slot * qvecSize]);
        slot++;
      }

      // Flush remaining
      if (slot > 0) {
        sqlite3_reset(stmtCell);
        sqlite3_bind_int(stmtCell, 1, c);
        sqlite3_bind_int(stmtCell, 2, slot);
        sqlite3_bind_blob(stmtCell, 3, val, cap / 8, SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmtCell, 4, rids, cap * (int)sizeof(i64), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmtCell, 5, vecs, cap * qvecSize, SQLITE_TRANSIENT);
        sqlite3_step(stmtCell);
        i64 flushed_cell_id = sqlite3_last_insert_rowid(p->db);

        for (int s = 0; s < slot; s++) {
          sqlite3_reset(stmtMap);
          sqlite3_bind_int64(stmtMap, 1, rids[s]);
          sqlite3_bind_int64(stmtMap, 2, flushed_cell_id);
          sqlite3_bind_int(stmtMap, 3, s);
          sqlite3_step(stmtMap);
        }
      }
    }

    sqlite3_free(val); sqlite3_free(rids); sqlite3_free(vecs);
    sqlite3_finalize(stmtCell); sqlite3_finalize(stmtMap);
  }

  sqlite3_free(qbuf);

  // Store full-precision vectors in _ivf_vectors when quantized
  if (quantizer != VEC0_IVF_QUANTIZER_NONE) {
    ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_VECTORS_NAME, col_idx);
    zSql = sqlite3_mprintf(
        "INSERT INTO " VEC0_SHADOW_IVF_VECTORS_NAME " (rowid, vector) VALUES (?, ?)",
        p->schemaName, p->tableName, col_idx);
    if (!zSql) { rc = SQLITE_NOMEM; goto train_error; }
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    if (rc != SQLITE_OK) goto train_error;
    for (int i = 0; i < N; i++) {
      sqlite3_reset(stmt);
      sqlite3_bind_int64(stmt, 1, rowids[i]);
      sqlite3_bind_blob(stmt, 2, &vectors[i * D], vecSize, SQLITE_STATIC);
      sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
  }

  // Set trained = 1
  {
    zSql = sqlite3_mprintf(
        "INSERT OR REPLACE INTO " VEC0_SHADOW_INFO_NAME " (key, value) VALUES ('ivf_trained_%d', '1')",
        p->schemaName, p->tableName, col_idx);
    if (zSql) { sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
      sqlite3_step(stmt); sqlite3_finalize(stmt); }
  }
  p->ivfTrainedCache[col_idx] = 1;

  sqlite3_exec(p->db, "RELEASE ivf_train", NULL, NULL, NULL);
  sqlite3_free(vectors); sqlite3_free(rowids); sqlite3_free(centroids); sqlite3_free(assignments);
  return SQLITE_OK;

train_error:
  sqlite3_exec(p->db, "ROLLBACK TO ivf_train", NULL, NULL, NULL);
  sqlite3_exec(p->db, "RELEASE ivf_train", NULL, NULL, NULL);
  sqlite3_free(vectors); sqlite3_free(rowids); sqlite3_free(centroids); sqlite3_free(assignments);
  return rc;
}

static int ivf_cmd_set_centroid(vec0_vtab *p, int col_idx, int centroid_id,
                                 const void *vectorData, int vectorSize) {
  sqlite3_stmt *stmt = NULL;
  int rc;
  int D = (int)p->vector_columns[col_idx].dimensions;
  if (vectorSize != (int)(D * sizeof(float))) { vtab_set_error(&p->base, "Dimension mismatch"); return SQLITE_ERROR; }

  char *zSql = sqlite3_mprintf(
      "INSERT OR REPLACE INTO " VEC0_SHADOW_IVF_CENTROIDS_NAME " (centroid_id, centroid) VALUES (?, ?)",
      p->schemaName, p->tableName, col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int(stmt, 1, centroid_id);
  sqlite3_bind_blob(stmt, 2, vectorData, vectorSize, SQLITE_STATIC);
  rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) return SQLITE_ERROR;

  zSql = sqlite3_mprintf(
      "INSERT OR REPLACE INTO " VEC0_SHADOW_INFO_NAME " (key, value) VALUES ('ivf_trained_%d', '1')",
      p->schemaName, p->tableName, col_idx);
  if (zSql) { sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    sqlite3_step(stmt); sqlite3_finalize(stmt); }
  p->ivfTrainedCache[col_idx] = 1;
  sqlite3_finalize(p->stmtIvfCentroidsAll[col_idx]); p->stmtIvfCentroidsAll[col_idx] = NULL;
  return SQLITE_OK;
}

static int ivf_cmd_assign_vectors(vec0_vtab *p, int col_idx) {
  if (!ivf_is_trained(p, col_idx)) { vtab_set_error(&p->base, "No centroids"); return SQLITE_ERROR; }

  int D = (int)p->vector_columns[col_idx].dimensions;
  int vecSize = D * (int)sizeof(float);
  int rc;
  sqlite3_stmt *stmt = NULL;
  char *zSql;

  // Load centroids
  int nlist = 0;
  float *centroids = NULL;
  zSql = sqlite3_mprintf("SELECT count(*) FROM " VEC0_SHADOW_IVF_CENTROIDS_NAME,
      p->schemaName, p->tableName, col_idx);
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) nlist = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  if (nlist == 0) { vtab_set_error(&p->base, "No centroids"); return SQLITE_ERROR; }

  centroids = sqlite3_malloc64((i64)nlist * D * sizeof(float));
  if (!centroids) return SQLITE_NOMEM;
  zSql = sqlite3_mprintf("SELECT centroid_id, centroid FROM " VEC0_SHADOW_IVF_CENTROIDS_NAME " ORDER BY centroid_id",
      p->schemaName, p->tableName, col_idx);
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
  { int ci = 0; while (sqlite3_step(stmt) == SQLITE_ROW && ci < nlist) {
      const void *b = sqlite3_column_blob(stmt, 1);
      int bBytes = sqlite3_column_bytes(stmt, 1);
      if (b && bBytes == vecSize) memcpy(&centroids[ci * D], b, vecSize);
      ci++;
  }}
  sqlite3_finalize(stmt);

  // Read unassigned cells, re-insert into trained cells
  zSql = sqlite3_mprintf(
      "SELECT rowid, n_vectors, validity, rowids, vectors FROM " VEC0_SHADOW_IVF_CELLS_NAME
      " WHERE centroid_id = %d",
      p->schemaName, p->tableName, col_idx, VEC0_IVF_UNASSIGNED_CENTROID_ID);
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);

  // Invalidate cached stmts since we'll be modifying cells
  ivf_invalidate_cached(p, col_idx);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int n = sqlite3_column_int(stmt, 1);
    const unsigned char *val = (const unsigned char *)sqlite3_column_blob(stmt, 2);
    const i64 *rids = (const i64 *)sqlite3_column_blob(stmt, 3);
    const float *vecs = (const float *)sqlite3_column_blob(stmt, 4);
    int valBytes = sqlite3_column_bytes(stmt, 2);
    int ridsBytes = sqlite3_column_bytes(stmt, 3);
    int vecsBytes = sqlite3_column_bytes(stmt, 4);
    if (!val || !rids || !vecs) continue;
    int cap = valBytes * 8;
    if (ridsBytes / (int)sizeof(i64) < cap) cap = ridsBytes / (int)sizeof(i64);
    if (vecsBytes / vecSize < cap) cap = vecsBytes / vecSize;

    for (int i = 0; i < cap && n > 0; i++) {
      if (!(val[i / 8] & (1 << (i % 8)))) continue;
      n--;
      int cid = ivf_find_nearest_centroid(p, col_idx, &vecs[i * D], centroids, D, nlist);

      // Delete old rowid_map entry
      sqlite3_stmt *sd = NULL;
      char *zd = sqlite3_mprintf("DELETE FROM " VEC0_SHADOW_IVF_ROWID_MAP_NAME " WHERE rowid = ?",
          p->schemaName, p->tableName, col_idx);
      if (zd) { sqlite3_prepare_v2(p->db, zd, -1, &sd, NULL); sqlite3_free(zd);
        sqlite3_bind_int64(sd, 1, rids[i]); sqlite3_step(sd); sqlite3_finalize(sd); }

      ivf_cell_insert(p, col_idx, cid, rids[i], &vecs[i * D], vecSize);
    }
  }
  sqlite3_finalize(stmt);

  // Delete unassigned cells
  zSql = sqlite3_mprintf(
      "DELETE FROM " VEC0_SHADOW_IVF_CELLS_NAME " WHERE centroid_id = %d",
      p->schemaName, p->tableName, col_idx, VEC0_IVF_UNASSIGNED_CENTROID_ID);
  if (zSql) { sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    sqlite3_step(stmt); sqlite3_finalize(stmt); }

  sqlite3_free(centroids);
  return SQLITE_OK;
}

static int ivf_cmd_clear_centroids(vec0_vtab *p, int col_idx) {
  float *vectors = NULL; i64 *rowids = NULL; int N = 0;
  int vecSize = ivf_vec_size(p, col_idx);
  int D = (int)p->vector_columns[col_idx].dimensions;
  int rc;
  sqlite3_stmt *stmt = NULL;
  char *zSql;

  rc = ivf_load_all_vectors(p, col_idx, &vectors, &rowids, &N);
  if (rc != SQLITE_OK) return rc;

  ivf_invalidate_cached(p, col_idx);

  ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_CENTROIDS_NAME, col_idx);
  ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_CELLS_NAME, col_idx);
  ivf_exec(p, "DELETE FROM " VEC0_SHADOW_IVF_ROWID_MAP_NAME, col_idx);

  // Re-insert all vectors into unassigned cells
  for (int i = 0; i < N; i++) {
    ivf_cell_insert(p, col_idx, VEC0_IVF_UNASSIGNED_CENTROID_ID,
                     rowids[i], &vectors[i * D], vecSize);
  }

  zSql = sqlite3_mprintf(
      "INSERT OR REPLACE INTO " VEC0_SHADOW_INFO_NAME " (key, value) VALUES ('ivf_trained_%d', '0')",
      p->schemaName, p->tableName, col_idx);
  if (zSql) { sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL); sqlite3_free(zSql);
    sqlite3_step(stmt); sqlite3_finalize(stmt); }
  p->ivfTrainedCache[col_idx] = 0;

  sqlite3_free(vectors); sqlite3_free(rowids);
  return SQLITE_OK;
}

// ============================================================================
// KNN Query — scan all cells for probed centroids
// ============================================================================

struct IvfCentroidDist { int id; float dist; };
struct IvfCandidate { i64 rowid; float distance; };

static int ivf_candidate_cmp(const void *a, const void *b) {
  float da = ((const struct IvfCandidate *)a)->distance;
  float db = ((const struct IvfCandidate *)b)->distance;
  if (da < db) return -1;
  if (da > db) return 1;
  return 0;
}

/**
 * Scan cell rows from a prepared statement, computing distances in-memory.
 * The statement must return (n_vectors, validity, rowids, vectors) columns.
 * queryVecQ is the quantized query (same type as cell vectors).
 * qvecSize is the size of one quantized vector in bytes.
 */
static int ivf_scan_cells_from_stmt(vec0_vtab *p, int col_idx,
                                     sqlite3_stmt *stmt,
                                     const void *queryVecQ, int qvecSize,
                                     struct IvfCandidate **candidates,
                                     int *nCandidates, int *cap) {
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int n = sqlite3_column_int(stmt, 0);
    if (n == 0) continue;
    const unsigned char *validity = (const unsigned char *)sqlite3_column_blob(stmt, 1);
    const i64 *rowids = (const i64 *)sqlite3_column_blob(stmt, 2);
    const unsigned char *vectors = (const unsigned char *)sqlite3_column_blob(stmt, 3);
    int valBytes = sqlite3_column_bytes(stmt, 1);
    int ridsBytes = sqlite3_column_bytes(stmt, 2);
    int vecsBytes = sqlite3_column_bytes(stmt, 3);
    if (!validity || !rowids || !vectors) continue;
    int cell_cap = valBytes * 8;
    if (ridsBytes / (int)sizeof(i64) < cell_cap) cell_cap = ridsBytes / (int)sizeof(i64);
    if (vecsBytes / qvecSize < cell_cap) cell_cap = vecsBytes / qvecSize;

    int found = 0;
    for (int i = 0; i < cell_cap && found < n; i++) {
      if (!(validity[i / 8] & (1 << (i % 8)))) continue;
      found++;
      if (*nCandidates >= *cap) {
        *cap *= 2;
        struct IvfCandidate *tmp = sqlite3_realloc64(*candidates, (i64)*cap * sizeof(struct IvfCandidate));
        if (!tmp) return SQLITE_NOMEM;
        *candidates = tmp;
      }
      (*candidates)[*nCandidates].rowid = rowids[i];
      (*candidates)[*nCandidates].distance = ivf_distance(p, col_idx,
          queryVecQ, &vectors[i * qvecSize]);
      (*nCandidates)++;
    }
  }
  return SQLITE_OK;
}

static int ivf_query_knn(vec0_vtab *p, int col_idx,
                          const void *queryVector, int queryVectorSize,
                          i64 k, struct vec0_query_knn_data *knn_data) {
  UNUSED_PARAMETER(queryVectorSize);
  int rc;
  int nprobe = p->vector_columns[col_idx].ivf.nprobe;
  int trained = ivf_is_trained(p, col_idx);
  int quantizer = p->vector_columns[col_idx].ivf.quantizer;
  int oversample = p->vector_columns[col_idx].ivf.oversample;
  int qvecSize = ivf_vec_size(p, col_idx);

  // Quantize query vector for scanning
  void *queryQ = sqlite3_malloc(qvecSize);
  if (!queryQ) return SQLITE_NOMEM;
  ivf_quantize(p, col_idx, (const float *)queryVector, queryQ);

  // With oversample, collect more candidates for re-ranking
  i64 collect_k = (oversample > 1) ? k * oversample : k;

  int cap = (collect_k < 1024) ? 1024 : (int)collect_k * 2;
  int nCandidates = 0;
  struct IvfCandidate *candidates = sqlite3_malloc64((i64)cap * sizeof(struct IvfCandidate));
  if (!candidates) { sqlite3_free(queryQ); return SQLITE_NOMEM; }

  if (trained) {
    // Find top nprobe centroids using quantized distance
    int nlist = 0;
    rc = ivf_ensure_stmt(p, &p->stmtIvfCentroidsAll[col_idx],
        "SELECT centroid_id, centroid FROM " VEC0_SHADOW_IVF_CENTROIDS_NAME, col_idx);
    if (rc != SQLITE_OK) { sqlite3_free(queryQ); sqlite3_free(candidates); return rc; }
    sqlite3_stmt *stmt = p->stmtIvfCentroidsAll[col_idx];
    sqlite3_reset(stmt);

    int centroid_cap = 64;
    struct IvfCentroidDist *cd = sqlite3_malloc64(centroid_cap * sizeof(*cd));
    if (!cd) { sqlite3_free(queryQ); sqlite3_free(candidates); return SQLITE_NOMEM; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      if (nlist >= centroid_cap) {
        centroid_cap *= 2;
        struct IvfCentroidDist *tmp = sqlite3_realloc64(cd, centroid_cap * sizeof(*cd));
        if (!tmp) { sqlite3_free(cd); sqlite3_free(queryQ); sqlite3_free(candidates); return SQLITE_NOMEM; }
        cd = tmp;
      }
      cd[nlist].id = sqlite3_column_int(stmt, 0);
      const void *c = sqlite3_column_blob(stmt, 1);
      int cBytes = sqlite3_column_bytes(stmt, 1);
      // Compare quantized query with quantized centroid
      cd[nlist].dist = (c && cBytes == qvecSize) ? ivf_distance(p, col_idx, queryQ, c) : FLT_MAX;
      nlist++;
    }

    int actual_nprobe = nprobe < nlist ? nprobe : nlist;
    for (int i = 0; i < actual_nprobe; i++) {
      int min_j = i;
      for (int j = i + 1; j < nlist; j++) {
        if (cd[j].dist < cd[min_j].dist) min_j = j;
      }
      if (min_j != i) { struct IvfCentroidDist tmp = cd[i]; cd[i] = cd[min_j]; cd[min_j] = tmp; }
    }

    // Scan probed cells + unassigned with quantized distance
    {
      sqlite3_str *s = sqlite3_str_new(NULL);
      sqlite3_str_appendf(s,
          "SELECT n_vectors, validity, rowids, vectors FROM " VEC0_SHADOW_IVF_CELLS_NAME
          " WHERE centroid_id IN (",
          p->schemaName, p->tableName, col_idx);
      for (int i = 0; i < actual_nprobe; i++) {
        if (i > 0) sqlite3_str_appendall(s, ",");
        sqlite3_str_appendf(s, "%d", cd[i].id);
      }
      sqlite3_str_appendf(s, ",%d)", VEC0_IVF_UNASSIGNED_CENTROID_ID);
      char *zSql = sqlite3_str_finish(s);
      if (!zSql) { sqlite3_free(cd); sqlite3_free(queryQ); sqlite3_free(candidates); return SQLITE_NOMEM; }

      sqlite3_stmt *stmtScan = NULL;
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtScan, NULL);
      sqlite3_free(zSql);
      if (rc != SQLITE_OK) { sqlite3_free(cd); sqlite3_free(queryQ); sqlite3_free(candidates); return rc; }

      rc = ivf_scan_cells_from_stmt(p, col_idx, stmtScan, queryQ, qvecSize,
                                     &candidates, &nCandidates, &cap);
      sqlite3_finalize(stmtScan);
      if (rc != SQLITE_OK) { sqlite3_free(cd); sqlite3_free(queryQ); sqlite3_free(candidates); return rc; }
    }

    sqlite3_free(cd);
  } else {
    // Flat mode: scan only unassigned cells
    sqlite3_stmt *stmtScan = NULL;
    char *zSql = sqlite3_mprintf(
        "SELECT n_vectors, validity, rowids, vectors FROM " VEC0_SHADOW_IVF_CELLS_NAME
        " WHERE centroid_id = %d",
        p->schemaName, p->tableName, col_idx, VEC0_IVF_UNASSIGNED_CENTROID_ID);
    if (!zSql) { sqlite3_free(queryQ); sqlite3_free(candidates); return SQLITE_NOMEM; }
    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtScan, NULL); sqlite3_free(zSql);
    if (rc == SQLITE_OK) {
      rc = ivf_scan_cells_from_stmt(p, col_idx, stmtScan, queryQ, qvecSize,
                                     &candidates, &nCandidates, &cap);
      sqlite3_finalize(stmtScan);
      if (rc != SQLITE_OK) { sqlite3_free(queryQ); sqlite3_free(candidates); return rc; }
    }
  }

  sqlite3_free(queryQ);

  // Sort candidates by quantized distance
  qsort(candidates, nCandidates, sizeof(struct IvfCandidate), ivf_candidate_cmp);

  // Oversample re-ranking: re-score top (oversample*k) with full-precision vectors
  if (oversample > 1 && quantizer != VEC0_IVF_QUANTIZER_NONE && nCandidates > 0) {
    i64 rescore_n = collect_k < nCandidates ? collect_k : nCandidates;
    sqlite3_stmt *stmtVec = NULL;
    char *zSql = sqlite3_mprintf(
        "SELECT vector FROM " VEC0_SHADOW_IVF_VECTORS_NAME " WHERE rowid = ?",
        p->schemaName, p->tableName, col_idx);
    if (zSql) {
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmtVec, NULL); sqlite3_free(zSql);
      if (rc == SQLITE_OK) {
        for (i64 i = 0; i < rescore_n; i++) {
          sqlite3_reset(stmtVec);
          sqlite3_bind_int64(stmtVec, 1, candidates[i].rowid);
          if (sqlite3_step(stmtVec) == SQLITE_ROW) {
            const float *fullVec = (const float *)sqlite3_column_blob(stmtVec, 0);
            int fullVecBytes = sqlite3_column_bytes(stmtVec, 0);
            if (fullVec && fullVecBytes == (int)p->vector_columns[col_idx].dimensions * (int)sizeof(float)) {
              candidates[i].distance = ivf_distance_float(p, col_idx,
                  (const float *)queryVector, fullVec);
            }
          }
        }
        sqlite3_finalize(stmtVec);
      }
    }
    // Re-sort after re-scoring
    qsort(candidates, (size_t)rescore_n, sizeof(struct IvfCandidate), ivf_candidate_cmp);
    nCandidates = (int)rescore_n;
  }

  qsort(candidates, nCandidates, sizeof(struct IvfCandidate), ivf_candidate_cmp);
  i64 nResults = nCandidates < k ? nCandidates : k;

  if (nResults == 0) {
    knn_data->rowids = NULL; knn_data->distances = NULL;
    knn_data->k = k; knn_data->k_used = 0; knn_data->current_idx = 0;
    sqlite3_free(candidates); return SQLITE_OK;
  }

  knn_data->rowids = sqlite3_malloc64(nResults * sizeof(i64));
  knn_data->distances = sqlite3_malloc64(nResults * sizeof(f32));
  if (!knn_data->rowids || !knn_data->distances) {
    sqlite3_free(knn_data->rowids); sqlite3_free(knn_data->distances);
    sqlite3_free(candidates); return SQLITE_NOMEM;
  }
  for (i64 i = 0; i < nResults; i++) {
    knn_data->rowids[i] = candidates[i].rowid;
    knn_data->distances[i] = candidates[i].distance;
  }
  knn_data->k = k; knn_data->k_used = nResults; knn_data->current_idx = 0;
  sqlite3_free(candidates);
  return SQLITE_OK;
}

// ============================================================================
// Command dispatch
// ============================================================================

static int ivf_handle_command(vec0_vtab *p, const char *command,
                               int argc, sqlite3_value **argv) {
  UNUSED_PARAMETER(argc);
  int col_idx = -1;
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type == VEC0_INDEX_TYPE_IVF) { col_idx = i; break; }
  }
  if (col_idx < 0) return SQLITE_EMPTY;

  // nprobe=N — change nprobe at runtime without rebuilding
  if (strncmp(command, "nprobe=", 7) == 0) {
    int new_nprobe = atoi(command + 7);
    if (new_nprobe < 1) {
      vtab_set_error(&p->base, "nprobe must be >= 1");
      return SQLITE_ERROR;
    }
    p->vector_columns[col_idx].ivf.nprobe = new_nprobe;
    return SQLITE_OK;
  }

  if (strcmp(command, "compute-centroids") == 0)
    return ivf_cmd_compute_centroids(p, col_idx, 0, VEC0_IVF_KMEANS_MAX_ITER, VEC0_IVF_KMEANS_DEFAULT_SEED);

  if (strncmp(command, "compute-centroids:", 18) == 0) {
    const char *json = command + 18;
    int nlist = 0, max_iter = VEC0_IVF_KMEANS_MAX_ITER;
    uint32_t seed = VEC0_IVF_KMEANS_DEFAULT_SEED;
    const char *pn = strstr(json, "\"nlist\":"); if (pn) nlist = atoi(pn + 8);
    const char *pi = strstr(json, "\"max_iterations\":"); if (pi) max_iter = atoi(pi + 17);
    const char *ps = strstr(json, "\"seed\":"); if (ps) seed = (uint32_t)atoi(ps + 7);
    return ivf_cmd_compute_centroids(p, col_idx, nlist, max_iter, seed);
  }

  if (strncmp(command, "set-centroid:", 13) == 0) {
    int centroid_id = atoi(command + 13);
    for (int i = 0; i < (int)(p->numVectorColumns + p->numPartitionColumns +
                               p->numAuxiliaryColumns + p->numMetadataColumns); i++) {
      if (p->user_column_kinds[i] == SQLITE_VEC0_USER_COLUMN_KIND_VECTOR &&
          p->user_column_idxs[i] == col_idx) {
        sqlite3_value *v = argv[2 + VEC0_COLUMN_USERN_START + i];
        if (sqlite3_value_type(v) == SQLITE_NULL) { vtab_set_error(&p->base, "set-centroid requires vector"); return SQLITE_ERROR; }
        return ivf_cmd_set_centroid(p, col_idx, centroid_id, sqlite3_value_blob(v), sqlite3_value_bytes(v));
      }
    }
    return SQLITE_ERROR;
  }

  if (strcmp(command, "assign-vectors") == 0) return ivf_cmd_assign_vectors(p, col_idx);
  if (strcmp(command, "clear-centroids") == 0) return ivf_cmd_clear_centroids(p, col_idx);
  return SQLITE_EMPTY;
}

#endif /* SQLITE_VEC_IVF_C */
