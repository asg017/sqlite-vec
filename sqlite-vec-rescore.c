/**
 * sqlite-vec-rescore.c — Rescore index logic for sqlite-vec.
 *
 * This file is #included into sqlite-vec.c after the vec0_vtab definition.
 * All functions receive a vec0_vtab *p and access p->vector_columns[i].rescore.
 *
 * Shadow tables per rescore-enabled vector column:
 *   _rescore_chunks{NN}  — quantized vectors in chunk layout (for coarse scan)
 *   _rescore_vectors{NN} — float vectors keyed by rowid (for fast rescore lookup)
 */

// ============================================================================
// Shadow table lifecycle
// ============================================================================

static int rescore_create_tables(vec0_vtab *p, sqlite3 *db, char **pzErr) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type != VEC0_INDEX_TYPE_RESCORE)
      continue;

    // Quantized chunk table (same layout as _vector_chunks)
    char *zSql = sqlite3_mprintf(
        "CREATE TABLE \"%w\".\"%w_rescore_chunks%02d\""
        "(rowid PRIMARY KEY, vectors BLOB NOT NULL)",
        p->schemaName, p->tableName, i);
    if (!zSql)
      return SQLITE_NOMEM;
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, zSql, -1, &stmt, 0);
    sqlite3_free(zSql);
    if ((rc != SQLITE_OK) || (sqlite3_step(stmt) != SQLITE_DONE)) {
      *pzErr = sqlite3_mprintf(
          "Could not create '_rescore_chunks%02d' shadow table: %s", i,
          sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return SQLITE_ERROR;
    }
    sqlite3_finalize(stmt);

    // Float vector table (rowid-keyed for fast random access)
    zSql = sqlite3_mprintf(
        "CREATE TABLE \"%w\".\"%w_rescore_vectors%02d\""
        "(rowid INTEGER PRIMARY KEY, vector BLOB NOT NULL)",
        p->schemaName, p->tableName, i);
    if (!zSql)
      return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(db, zSql, -1, &stmt, 0);
    sqlite3_free(zSql);
    if ((rc != SQLITE_OK) || (sqlite3_step(stmt) != SQLITE_DONE)) {
      *pzErr = sqlite3_mprintf(
          "Could not create '_rescore_vectors%02d' shadow table: %s", i,
          sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return SQLITE_ERROR;
    }
    sqlite3_finalize(stmt);
  }
  return SQLITE_OK;
}

static int rescore_drop_tables(vec0_vtab *p) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    sqlite3_stmt *stmt;
    int rc;
    char *zSql;

    if (p->shadowRescoreChunksNames[i]) {
      zSql = sqlite3_mprintf("DROP TABLE IF EXISTS \"%w\".\"%w\"",
                              p->schemaName, p->shadowRescoreChunksNames[i]);
      if (!zSql)
        return SQLITE_NOMEM;
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, 0);
      sqlite3_free(zSql);
      if ((rc != SQLITE_OK) || (sqlite3_step(stmt) != SQLITE_DONE)) {
        sqlite3_finalize(stmt);
        return SQLITE_ERROR;
      }
      sqlite3_finalize(stmt);
    }

    if (p->shadowRescoreVectorsNames[i]) {
      zSql = sqlite3_mprintf("DROP TABLE IF EXISTS \"%w\".\"%w\"",
                              p->schemaName, p->shadowRescoreVectorsNames[i]);
      if (!zSql)
        return SQLITE_NOMEM;
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, 0);
      sqlite3_free(zSql);
      if ((rc != SQLITE_OK) || (sqlite3_step(stmt) != SQLITE_DONE)) {
        sqlite3_finalize(stmt);
        return SQLITE_ERROR;
      }
      sqlite3_finalize(stmt);
    }
  }
  return SQLITE_OK;
}

static size_t rescore_quantized_byte_size(struct VectorColumnDefinition *col) {
  switch (col->rescore.quantizer_type) {
  case VEC0_RESCORE_QUANTIZER_BIT:
    return col->dimensions / CHAR_BIT;
  case VEC0_RESCORE_QUANTIZER_INT8:
    return col->dimensions;
  default:
    return 0;
  }
}

/**
 * Insert a new chunk row into each _rescore_chunks{NN} table with a zeroblob.
 */
static int rescore_new_chunk(vec0_vtab *p, i64 chunk_rowid) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type != VEC0_INDEX_TYPE_RESCORE)
      continue;
    size_t quantized_size =
        rescore_quantized_byte_size(&p->vector_columns[i]);
    i64 blob_size = (i64)p->chunk_size * (i64)quantized_size;

    char *zSql = sqlite3_mprintf(
        "INSERT INTO \"%w\".\"%w\"(_rowid_, rowid, vectors) VALUES (?, ?, ?)",
        p->schemaName, p->shadowRescoreChunksNames[i]);
    if (!zSql)
      return SQLITE_NOMEM;
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) {
      sqlite3_finalize(stmt);
      return rc;
    }
    sqlite3_bind_int64(stmt, 1, chunk_rowid);
    sqlite3_bind_int64(stmt, 2, chunk_rowid);
    sqlite3_bind_zeroblob64(stmt, 3, blob_size);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
      return rc;
  }
  return SQLITE_OK;
}

// ============================================================================
// Quantization
// ============================================================================

static void rescore_quantize_float_to_bit(const float *src, uint8_t *dst,
                                          size_t dimensions) {
  memset(dst, 0, dimensions / CHAR_BIT);
  for (size_t i = 0; i < dimensions; i++) {
    if (src[i] >= 0.0f) {
      dst[i / CHAR_BIT] |= (1 << (i % CHAR_BIT));
    }
  }
}

static void rescore_quantize_float_to_int8(const float *src, int8_t *dst,
                                           size_t dimensions) {
  float step = 2.0f / 255.0f;
  for (size_t i = 0; i < dimensions; i++) {
    float v = (src[i] - (-1.0f)) / step - 128.0f;
    if (!(v <= 127.0f)) v = 127.0f;
    if (!(v >= -128.0f)) v = -128.0f;
    dst[i] = (int8_t)v;
  }
}

// ============================================================================
// Insert path
// ============================================================================

/**
 * Quantize float vector to _rescore_chunks and store in _rescore_vectors.
 */
static int rescore_on_insert(vec0_vtab *p, i64 chunk_rowid, i64 chunk_offset,
                             i64 rowid, void *vectorDatas[]) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type != VEC0_INDEX_TYPE_RESCORE)
      continue;

    struct VectorColumnDefinition *col = &p->vector_columns[i];
    size_t qsize = rescore_quantized_byte_size(col);
    size_t fsize = vector_column_byte_size(*col);
    int rc;

    // 1. Write quantized vector to _rescore_chunks blob
    {
      void *qbuf = sqlite3_malloc(qsize);
      if (!qbuf)
        return SQLITE_NOMEM;

      switch (col->rescore.quantizer_type) {
      case VEC0_RESCORE_QUANTIZER_BIT:
        rescore_quantize_float_to_bit((const float *)vectorDatas[i],
                                      (uint8_t *)qbuf, col->dimensions);
        break;
      case VEC0_RESCORE_QUANTIZER_INT8:
        rescore_quantize_float_to_int8((const float *)vectorDatas[i],
                                       (int8_t *)qbuf, col->dimensions);
        break;
      }

      sqlite3_blob *blob = NULL;
      rc = sqlite3_blob_open(p->db, p->schemaName,
                             p->shadowRescoreChunksNames[i], "vectors",
                             chunk_rowid, 1, &blob);
      if (rc != SQLITE_OK) {
        sqlite3_free(qbuf);
        return rc;
      }
      rc = sqlite3_blob_write(blob, qbuf, qsize, chunk_offset * qsize);
      sqlite3_free(qbuf);
      int brc = sqlite3_blob_close(blob);
      if (rc != SQLITE_OK)
        return rc;
      if (brc != SQLITE_OK)
        return brc;
    }

    // 2. Insert float vector into _rescore_vectors (rowid-keyed)
    {
      char *zSql = sqlite3_mprintf(
          "INSERT INTO \"%w\".\"%w\"(rowid, vector) VALUES (?, ?)",
          p->schemaName, p->shadowRescoreVectorsNames[i]);
      if (!zSql)
        return SQLITE_NOMEM;
      sqlite3_stmt *stmt;
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
      sqlite3_free(zSql);
      if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return rc;
      }
      sqlite3_bind_int64(stmt, 1, rowid);
      sqlite3_bind_blob(stmt, 2, vectorDatas[i], fsize, SQLITE_TRANSIENT);
      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      if (rc != SQLITE_DONE)
        return SQLITE_ERROR;
    }
  }
  return SQLITE_OK;
}

// ============================================================================
// Delete path
// ============================================================================

/**
 * Zero out quantized vector in _rescore_chunks and delete from _rescore_vectors.
 */
static int rescore_on_delete(vec0_vtab *p, i64 chunk_id, u64 chunk_offset,
                             i64 rowid) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type != VEC0_INDEX_TYPE_RESCORE)
      continue;
    int rc;

    // 1. Zero out quantized data in _rescore_chunks
    {
      size_t qsize = rescore_quantized_byte_size(&p->vector_columns[i]);
      void *zeroBuf = sqlite3_malloc(qsize);
      if (!zeroBuf)
        return SQLITE_NOMEM;
      memset(zeroBuf, 0, qsize);

      sqlite3_blob *blob = NULL;
      rc = sqlite3_blob_open(p->db, p->schemaName,
                             p->shadowRescoreChunksNames[i], "vectors",
                             chunk_id, 1, &blob);
      if (rc != SQLITE_OK) {
        sqlite3_free(zeroBuf);
        return rc;
      }
      rc = sqlite3_blob_write(blob, zeroBuf, qsize, chunk_offset * qsize);
      sqlite3_free(zeroBuf);
      int brc = sqlite3_blob_close(blob);
      if (rc != SQLITE_OK)
        return rc;
      if (brc != SQLITE_OK)
        return brc;
    }

    // 2. Delete from _rescore_vectors
    {
      char *zSql = sqlite3_mprintf(
          "DELETE FROM \"%w\".\"%w\" WHERE rowid = ?",
          p->schemaName, p->shadowRescoreVectorsNames[i]);
      if (!zSql)
        return SQLITE_NOMEM;
      sqlite3_stmt *stmt;
      rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
      sqlite3_free(zSql);
      if (rc != SQLITE_OK)
        return rc;
      sqlite3_bind_int64(stmt, 1, rowid);
      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      if (rc != SQLITE_DONE)
        return SQLITE_ERROR;
    }
  }
  return SQLITE_OK;
}

/**
 * Delete a chunk row from _rescore_chunks{NN} tables.
 * (_rescore_vectors rows were already deleted per-row in rescore_on_delete)
 */
static int rescore_delete_chunk(vec0_vtab *p, i64 chunk_id) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (!p->shadowRescoreChunksNames[i])
      continue;
    char *zSql = sqlite3_mprintf(
        "DELETE FROM \"%w\".\"%w\" WHERE rowid = ?",
        p->schemaName, p->shadowRescoreChunksNames[i]);
    if (!zSql)
      return SQLITE_NOMEM;
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK)
      return rc;
    sqlite3_bind_int64(stmt, 1, chunk_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
      return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

// ============================================================================
// KNN rescore query
// ============================================================================

/**
 * Phase 1: Coarse scan of quantized chunks → top k*oversample candidates (rowids).
 * Phase 2: For each candidate, blob_open _rescore_vectors by rowid, read float
 *          vector, compute float distance. Sort, return top k.
 *
 * Phase 2 is fast because _rescore_vectors has INTEGER PRIMARY KEY, so
 * sqlite3_blob_open/reopen addresses rows directly by rowid — no index lookup.
 */
static int rescore_knn(vec0_vtab *p, vec0_cursor *pCur,
                       struct VectorColumnDefinition *vector_column,
                       int vectorColumnIdx, struct Array *arrayRowidsIn,
                       struct Array *aMetadataIn, const char *idxStr, int argc,
                       sqlite3_value **argv, void *queryVector, i64 k,
                       struct vec0_query_knn_data *knn_data) {
  (void)pCur;
  (void)aMetadataIn;
  int rc = SQLITE_OK;
  int oversample = vector_column->rescore.oversample_search > 0
      ? vector_column->rescore.oversample_search
      : vector_column->rescore.oversample;
  i64 k_oversample = k * oversample;
  if (k_oversample > 4096)
    k_oversample = 4096;

  size_t qdim = vector_column->dimensions;
  size_t qsize = rescore_quantized_byte_size(vector_column);
  size_t fsize = vector_column_byte_size(*vector_column);

  // Quantize the query vector
  void *quantizedQuery = sqlite3_malloc(qsize);
  if (!quantizedQuery)
    return SQLITE_NOMEM;

  switch (vector_column->rescore.quantizer_type) {
  case VEC0_RESCORE_QUANTIZER_BIT:
    rescore_quantize_float_to_bit((const float *)queryVector,
                                  (uint8_t *)quantizedQuery, qdim);
    break;
  case VEC0_RESCORE_QUANTIZER_INT8:
    rescore_quantize_float_to_int8((const float *)queryVector,
                                   (int8_t *)quantizedQuery, qdim);
    break;
  }

  // Phase 1: Scan quantized chunks for k*oversample candidates
  sqlite3_stmt *stmtChunks = NULL;
  rc = vec0_chunks_iter(p, idxStr, argc, argv, &stmtChunks);
  if (rc != SQLITE_OK) {
    sqlite3_free(quantizedQuery);
    return rc;
  }

  i64 *cand_rowids = sqlite3_malloc(k_oversample * sizeof(i64));
  f32 *cand_distances = sqlite3_malloc(k_oversample * sizeof(f32));
  i64 *tmp_rowids = sqlite3_malloc(k_oversample * sizeof(i64));
  f32 *tmp_distances = sqlite3_malloc(k_oversample * sizeof(f32));
  f32 *chunk_distances = sqlite3_malloc(p->chunk_size * sizeof(f32));
  i32 *chunk_topk_idxs = sqlite3_malloc(k_oversample * sizeof(i32));
  u8 *b = sqlite3_malloc(p->chunk_size / CHAR_BIT);
  u8 *bTaken = sqlite3_malloc(p->chunk_size / CHAR_BIT);
  u8 *bmRowids = NULL;
  void *baseVectors = sqlite3_malloc((i64)p->chunk_size * (i64)qsize);

  if (!cand_rowids || !cand_distances || !tmp_rowids || !tmp_distances ||
      !chunk_distances || !chunk_topk_idxs || !b || !bTaken || !baseVectors) {
    rc = SQLITE_NOMEM;
    goto cleanup;
  }
  memset(cand_rowids, 0, k_oversample * sizeof(i64));
  memset(cand_distances, 0, k_oversample * sizeof(f32));

  if (arrayRowidsIn) {
    bmRowids = sqlite3_malloc(p->chunk_size / CHAR_BIT);
    if (!bmRowids) {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  }

  i64 cand_used = 0;

  while (1) {
    rc = sqlite3_step(stmtChunks);
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW) {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

    i64 chunk_id = sqlite3_column_int64(stmtChunks, 0);
    unsigned char *chunkValidity =
        (unsigned char *)sqlite3_column_blob(stmtChunks, 1);
    i64 *chunkRowids = (i64 *)sqlite3_column_blob(stmtChunks, 2);
    int validityBytes = sqlite3_column_bytes(stmtChunks, 1);
    int rowidsBytes = sqlite3_column_bytes(stmtChunks, 2);
    if (!chunkValidity || !chunkRowids) {
      rc = SQLITE_ERROR;
      goto cleanup;
    }
    // Validate blob sizes match chunk_size expectations
    if (validityBytes < (p->chunk_size + 7) / 8 ||
        rowidsBytes < p->chunk_size * (int)sizeof(i64)) {
      rc = SQLITE_ERROR;
      goto cleanup;
    }

    memset(chunk_distances, 0, p->chunk_size * sizeof(f32));
    memset(chunk_topk_idxs, 0, k_oversample * sizeof(i32));
    bitmap_copy(b, chunkValidity, p->chunk_size);

    if (arrayRowidsIn) {
      bitmap_clear(bmRowids, p->chunk_size);
      for (int j = 0; j < p->chunk_size; j++) {
        if (!bitmap_get(chunkValidity, j))
          continue;
        i64 rid = chunkRowids[j];
        void *found = bsearch(&rid, arrayRowidsIn->z, arrayRowidsIn->length,
                               sizeof(i64), _cmp);
        bitmap_set(bmRowids, j, found ? 1 : 0);
      }
      bitmap_and_inplace(b, bmRowids, p->chunk_size);
    }

    // Read quantized vectors
    sqlite3_blob *blobQ = NULL;
    rc = sqlite3_blob_open(p->db, p->schemaName,
                           p->shadowRescoreChunksNames[vectorColumnIdx],
                           "vectors", chunk_id, 0, &blobQ);
    if (rc != SQLITE_OK)
      goto cleanup;
    rc = sqlite3_blob_read(blobQ, baseVectors,
                           (i64)p->chunk_size * (i64)qsize, 0);
    sqlite3_blob_close(blobQ);
    if (rc != SQLITE_OK)
      goto cleanup;

    // Compute quantized distances
    for (int j = 0; j < p->chunk_size; j++) {
      if (!bitmap_get(b, j))
        continue;
      f32 dist = FLT_MAX;
      switch (vector_column->rescore.quantizer_type) {
      case VEC0_RESCORE_QUANTIZER_BIT: {
        const u8 *base_j = ((u8 *)baseVectors) + (j * (qdim / CHAR_BIT));
        dist = distance_hamming(base_j, (u8 *)quantizedQuery, &qdim);
        break;
      }
      case VEC0_RESCORE_QUANTIZER_INT8: {
        const i8 *base_j = ((i8 *)baseVectors) + (j * qdim);
        switch (vector_column->distance_metric) {
        case VEC0_DISTANCE_METRIC_L2:
          dist = distance_l2_sqr_int8(base_j, (i8 *)quantizedQuery, &qdim);
          break;
        case VEC0_DISTANCE_METRIC_COSINE:
          dist = distance_cosine_int8(base_j, (i8 *)quantizedQuery, &qdim);
          break;
        case VEC0_DISTANCE_METRIC_L1:
          dist = (f32)distance_l1_int8(base_j, (i8 *)quantizedQuery, &qdim);
          break;
        }
        break;
      }
      }
      chunk_distances[j] = dist;
    }

    int used1;
    min_idx(chunk_distances, p->chunk_size, b, chunk_topk_idxs,
            min(k_oversample, p->chunk_size), bTaken, &used1);

    i64 merged_used;
    merge_sorted_lists(cand_distances, cand_rowids, cand_used, chunk_distances,
                       chunkRowids, chunk_topk_idxs,
                       min(min(k_oversample, p->chunk_size), used1),
                       tmp_distances, tmp_rowids, k_oversample, &merged_used);

    for (i64 j = 0; j < merged_used; j++) {
      cand_rowids[j] = tmp_rowids[j];
      cand_distances[j] = tmp_distances[j];
    }
    cand_used = merged_used;
  }
  rc = SQLITE_OK;

  // Phase 2: Rescore candidates using _rescore_vectors (rowid-keyed)
  if (cand_used == 0) {
    knn_data->current_idx = 0;
    knn_data->k = 0;
    knn_data->rowids = NULL;
    knn_data->distances = NULL;
    knn_data->k_used = 0;
    goto cleanup;
  }
  {
    f32 *float_distances = sqlite3_malloc(cand_used * sizeof(f32));
    void *fBuf = sqlite3_malloc(fsize);
    if (!float_distances || !fBuf) {
      sqlite3_free(float_distances);
      sqlite3_free(fBuf);
      rc = SQLITE_NOMEM;
      goto cleanup;
    }

    // Open blob on _rescore_vectors, then reopen for each candidate rowid.
    // blob_reopen is O(1) for INTEGER PRIMARY KEY tables.
    sqlite3_blob *blobFloat = NULL;
    rc = sqlite3_blob_open(p->db, p->schemaName,
                           p->shadowRescoreVectorsNames[vectorColumnIdx],
                           "vector", cand_rowids[0], 0, &blobFloat);
    if (rc != SQLITE_OK) {
      sqlite3_free(float_distances);
      sqlite3_free(fBuf);
      goto cleanup;
    }

    rc = sqlite3_blob_read(blobFloat, fBuf, fsize, 0);
    if (rc != SQLITE_OK) {
      sqlite3_blob_close(blobFloat);
      sqlite3_free(float_distances);
      sqlite3_free(fBuf);
      goto cleanup;
    }
    float_distances[0] =
        vec0_distance_full(fBuf, queryVector, vector_column->dimensions,
                           vector_column->element_type,
                           vector_column->distance_metric);

    for (i64 j = 1; j < cand_used; j++) {
      rc = sqlite3_blob_reopen(blobFloat, cand_rowids[j]);
      if (rc != SQLITE_OK) {
        sqlite3_blob_close(blobFloat);
        sqlite3_free(float_distances);
        sqlite3_free(fBuf);
        goto cleanup;
      }
      rc = sqlite3_blob_read(blobFloat, fBuf, fsize, 0);
      if (rc != SQLITE_OK) {
        sqlite3_blob_close(blobFloat);
        sqlite3_free(float_distances);
        sqlite3_free(fBuf);
        goto cleanup;
      }
      float_distances[j] =
          vec0_distance_full(fBuf, queryVector, vector_column->dimensions,
                             vector_column->element_type,
                             vector_column->distance_metric);
    }
    sqlite3_blob_close(blobFloat);
    sqlite3_free(fBuf);

    // Sort by float distance
    for (i64 a = 0; a + 1 < cand_used; a++) {
      i64 minIdx = a;
      for (i64 c = a + 1; c < cand_used; c++) {
        if (float_distances[c] < float_distances[minIdx])
          minIdx = c;
      }
      if (minIdx != a) {
        f32 td = float_distances[a];
        float_distances[a] = float_distances[minIdx];
        float_distances[minIdx] = td;
        i64 tr = cand_rowids[a];
        cand_rowids[a] = cand_rowids[minIdx];
        cand_rowids[minIdx] = tr;
      }
    }

    i64 result_k = min(k, cand_used);
    i64 *out_rowids = sqlite3_malloc(result_k * sizeof(i64));
    f32 *out_distances = sqlite3_malloc(result_k * sizeof(f32));
    if (!out_rowids || !out_distances) {
      sqlite3_free(out_rowids);
      sqlite3_free(out_distances);
      sqlite3_free(float_distances);
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
    for (i64 j = 0; j < result_k; j++) {
      out_rowids[j] = cand_rowids[j];
      out_distances[j] = float_distances[j];
    }

    knn_data->current_idx = 0;
    knn_data->k = result_k;
    knn_data->rowids = out_rowids;
    knn_data->distances = out_distances;
    knn_data->k_used = result_k;

    sqlite3_free(float_distances);
  }

cleanup:
  sqlite3_finalize(stmtChunks);
  sqlite3_free(quantizedQuery);
  sqlite3_free(cand_rowids);
  sqlite3_free(cand_distances);
  sqlite3_free(tmp_rowids);
  sqlite3_free(tmp_distances);
  sqlite3_free(chunk_distances);
  sqlite3_free(chunk_topk_idxs);
  sqlite3_free(b);
  sqlite3_free(bTaken);
  sqlite3_free(bmRowids);
  sqlite3_free(baseVectors);
  return rc;
}

/**
 * Handle FTS5-style command dispatch for rescore parameters.
 * Returns SQLITE_OK if handled, SQLITE_EMPTY if not a rescore command.
 */
static int rescore_handle_command(vec0_vtab *p, const char *command) {
  if (strncmp(command, "oversample=", 11) == 0) {
    int val = atoi(command + 11);
    if (val < 1) {
      vtab_set_error(&p->base, "oversample must be >= 1");
      return SQLITE_ERROR;
    }
    for (int i = 0; i < p->numVectorColumns; i++) {
      if (p->vector_columns[i].index_type == VEC0_INDEX_TYPE_RESCORE) {
        p->vector_columns[i].rescore.oversample_search = val;
      }
    }
    return SQLITE_OK;
  }
  return SQLITE_EMPTY;
}

#ifdef SQLITE_VEC_TEST
void _test_rescore_quantize_float_to_bit(const float *src, uint8_t *dst, size_t dim) {
  rescore_quantize_float_to_bit(src, dst, dim);
}
void _test_rescore_quantize_float_to_int8(const float *src, int8_t *dst, size_t dim) {
  rescore_quantize_float_to_int8(src, dst, dim);
}
size_t _test_rescore_quantized_byte_size_bit(size_t dimensions) {
  struct VectorColumnDefinition col;
  memset(&col, 0, sizeof(col));
  col.dimensions = dimensions;
  col.rescore.quantizer_type = VEC0_RESCORE_QUANTIZER_BIT;
  return rescore_quantized_byte_size(&col);
}
size_t _test_rescore_quantized_byte_size_int8(size_t dimensions) {
  struct VectorColumnDefinition col;
  memset(&col, 0, sizeof(col));
  col.dimensions = dimensions;
  col.rescore.quantizer_type = VEC0_RESCORE_QUANTIZER_INT8;
  return rescore_quantized_byte_size(&col);
}
#endif
