// DiskANN algorithm implementation
// This file is #include'd into sqlite-vec.c — not compiled separately.

// ============================================================
// DiskANN node blob encode/decode functions
// ============================================================

/** Compute size of validity bitmap in bytes. */
int diskann_validity_byte_size(int n_neighbors) {
  return n_neighbors / CHAR_BIT;
}

/** Compute size of neighbor IDs blob in bytes. */
size_t diskann_neighbor_ids_byte_size(int n_neighbors) {
  return (size_t)n_neighbors * sizeof(i64);
}

/** Compute size of quantized vectors blob in bytes. */
size_t diskann_neighbor_qvecs_byte_size(
    int n_neighbors, enum Vec0DiskannQuantizerType quantizer_type,
    size_t dimensions) {
  return (size_t)n_neighbors *
         diskann_quantized_vector_byte_size(quantizer_type, dimensions);
}

/**
 * Create empty blobs for a new DiskANN node (all neighbors invalid).
 * Caller must free the returned pointers with sqlite3_free().
 */
int diskann_node_init(
    int n_neighbors, enum Vec0DiskannQuantizerType quantizer_type,
    size_t dimensions,
    u8 **outValidity, int *outValiditySize,
    u8 **outNeighborIds, int *outNeighborIdsSize,
    u8 **outNeighborQvecs, int *outNeighborQvecsSize) {

  int validitySize = diskann_validity_byte_size(n_neighbors);
  size_t idsSize = diskann_neighbor_ids_byte_size(n_neighbors);
  size_t qvecsSize = diskann_neighbor_qvecs_byte_size(
      n_neighbors, quantizer_type, dimensions);

  u8 *validity = sqlite3_malloc(validitySize);
  u8 *ids = sqlite3_malloc(idsSize);
  u8 *qvecs = sqlite3_malloc(qvecsSize);

  if (!validity || !ids || !qvecs) {
    sqlite3_free(validity);
    sqlite3_free(ids);
    sqlite3_free(qvecs);
    return SQLITE_NOMEM;
  }

  memset(validity, 0, validitySize);
  memset(ids, 0, idsSize);
  memset(qvecs, 0, qvecsSize);

  *outValidity = validity;       *outValiditySize = validitySize;
  *outNeighborIds = ids;         *outNeighborIdsSize = (int)idsSize;
  *outNeighborQvecs = qvecs;     *outNeighborQvecsSize = (int)qvecsSize;
  return SQLITE_OK;
}

/** Check if neighbor slot i is valid. */
int diskann_validity_get(const u8 *validity, int i) {
  return (validity[i / CHAR_BIT] >> (i % CHAR_BIT)) & 1;
}

/** Set neighbor slot i as valid (1) or invalid (0). */
void diskann_validity_set(u8 *validity, int i, int value) {
  if (value) {
    validity[i / CHAR_BIT] |= (1 << (i % CHAR_BIT));
  } else {
    validity[i / CHAR_BIT] &= ~(1 << (i % CHAR_BIT));
  }
}

/** Count the number of valid neighbors. */
int diskann_validity_count(const u8 *validity, int n_neighbors) {
  int count = 0;
  for (int i = 0; i < n_neighbors; i++) {
    count += diskann_validity_get(validity, i);
  }
  return count;
}

/** Get the rowid of the neighbor in slot i. */
i64 diskann_neighbor_id_get(const u8 *neighbor_ids, int i) {
  i64 result;
  memcpy(&result, neighbor_ids + i * sizeof(i64), sizeof(i64));
  return result;
}

/** Set the rowid of the neighbor in slot i. */
void diskann_neighbor_id_set(u8 *neighbor_ids, int i, i64 rowid) {
  memcpy(neighbor_ids + i * sizeof(i64), &rowid, sizeof(i64));
}

/** Get a pointer to the quantized vector in slot i (read-only). */
const u8 *diskann_neighbor_qvec_get(
    const u8 *qvecs, int i,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions) {
  size_t qvec_size = diskann_quantized_vector_byte_size(quantizer_type, dimensions);
  return qvecs + (size_t)i * qvec_size;
}

/** Copy a quantized vector into slot i. */
void diskann_neighbor_qvec_set(
    u8 *qvecs, int i, const u8 *src_qvec,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions) {
  size_t qvec_size = diskann_quantized_vector_byte_size(quantizer_type, dimensions);
  memcpy(qvecs + (size_t)i * qvec_size, src_qvec, qvec_size);
}

/**
 * Set neighbor slot i with a rowid and quantized vector, and mark as valid.
 */
void diskann_node_set_neighbor(
    u8 *validity, u8 *neighbor_ids, u8 *qvecs, int i,
    i64 neighbor_rowid, const u8 *neighbor_qvec,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions) {
  diskann_validity_set(validity, i, 1);
  diskann_neighbor_id_set(neighbor_ids, i, neighbor_rowid);
  diskann_neighbor_qvec_set(qvecs, i, neighbor_qvec, quantizer_type, dimensions);
}

/**
 * Clear neighbor slot i (mark invalid, zero out data).
 */
void diskann_node_clear_neighbor(
    u8 *validity, u8 *neighbor_ids, u8 *qvecs, int i,
    enum Vec0DiskannQuantizerType quantizer_type, size_t dimensions) {
  diskann_validity_set(validity, i, 0);
  diskann_neighbor_id_set(neighbor_ids, i, 0);
  size_t qvec_size = diskann_quantized_vector_byte_size(quantizer_type, dimensions);
  memset(qvecs + (size_t)i * qvec_size, 0, qvec_size);
}

/**
 * Quantize a full-precision float32 vector into the target quantizer format.
 * Output buffer must be pre-allocated with diskann_quantized_vector_byte_size() bytes.
 */
int diskann_quantize_vector(
    const f32 *src, size_t dimensions,
    enum Vec0DiskannQuantizerType quantizer_type,
    u8 *out) {

  switch (quantizer_type) {
    case VEC0_DISKANN_QUANTIZER_BINARY: {
      memset(out, 0, dimensions / CHAR_BIT);
      for (size_t i = 0; i < dimensions; i++) {
        if (src[i] > 0.0f) {
          out[i / CHAR_BIT] |= (1 << (i % CHAR_BIT));
        }
      }
      return SQLITE_OK;
    }
    case VEC0_DISKANN_QUANTIZER_INT8: {
      f32 step = (1.0f - (-1.0f)) / 255.0f;
      for (size_t i = 0; i < dimensions; i++) {
        ((i8 *)out)[i] = (i8)(((src[i] - (-1.0f)) / step) - 128.0f);
      }
      return SQLITE_OK;
    }
  }
  return SQLITE_ERROR;
}

/**
 * Compute approximate distance between a full-precision query vector and a
 * quantized neighbor vector. Used during graph traversal.
 */
/**
 * Compute distance between a pre-quantized query and a quantized neighbor.
 * The caller is responsible for quantizing the query vector once and passing
 * the result here for each neighbor comparison.
 */
static f32 diskann_distance_quantized_precomputed(
    const u8 *query_quantized, const u8 *quantized_neighbor,
    size_t dimensions,
    enum Vec0DiskannQuantizerType quantizer_type,
    enum Vec0DistanceMetrics distance_metric) {

  switch (quantizer_type) {
    case VEC0_DISKANN_QUANTIZER_BINARY:
      return distance_hamming(query_quantized, quantized_neighbor, &dimensions);
    case VEC0_DISKANN_QUANTIZER_INT8: {
      switch (distance_metric) {
        case VEC0_DISTANCE_METRIC_L2:
          return distance_l2_sqr_int8(query_quantized, quantized_neighbor, &dimensions);
        case VEC0_DISTANCE_METRIC_COSINE:
          return distance_cosine_int8(query_quantized, quantized_neighbor, &dimensions);
        case VEC0_DISTANCE_METRIC_L1:
          return (f32)distance_l1_int8(query_quantized, quantized_neighbor, &dimensions);
      }
      break;
    }
  }
  return FLT_MAX;
}

/**
 * Quantize a float query vector. Returns allocated buffer (caller must free).
 */
static u8 *diskann_quantize_query(
    const f32 *query_vector, size_t dimensions,
    enum Vec0DiskannQuantizerType quantizer_type) {
  size_t qsize = diskann_quantized_vector_byte_size(quantizer_type, dimensions);
  u8 *buf = sqlite3_malloc(qsize);
  if (!buf) return NULL;
  diskann_quantize_vector(query_vector, dimensions, quantizer_type, buf);
  return buf;
}

/**
 * Legacy wrapper: quantizes on-the-fly (used by callers that don't pre-quantize).
 */
f32 diskann_distance_quantized(
    const void *query_vector, const u8 *quantized_neighbor,
    size_t dimensions,
    enum Vec0DiskannQuantizerType quantizer_type,
    enum Vec0DistanceMetrics distance_metric) {

  u8 *query_q = diskann_quantize_query((const f32 *)query_vector, dimensions, quantizer_type);
  if (!query_q) return FLT_MAX;
  f32 dist = diskann_distance_quantized_precomputed(
      query_q, quantized_neighbor, dimensions, quantizer_type, distance_metric);
  sqlite3_free(query_q);
  return dist;
}

// ============================================================
// DiskANN medoid / entry point management
// ============================================================

/**
 * Get the current medoid rowid for the given vector column's DiskANN index.
 * Returns SQLITE_OK with *outMedoid set to the medoid rowid.
 * If the graph is empty, returns SQLITE_OK with *outIsEmpty = 1.
 */
static int diskann_medoid_get(vec0_vtab *p, int vec_col_idx,
                               i64 *outMedoid, int *outIsEmpty) {
  int rc;
  sqlite3_stmt *stmt = NULL;
  char *key = sqlite3_mprintf("diskann_medoid_%02d", vec_col_idx);
  char *zSql = sqlite3_mprintf(
      "SELECT value FROM " VEC0_SHADOW_INFO_NAME " WHERE key = ?",
      p->schemaName, p->tableName);
  if (!key || !zSql) {
    sqlite3_free(key);
    sqlite3_free(zSql);
    return SQLITE_NOMEM;
  }

  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) {
    sqlite3_free(key);
    return rc;
  }

  sqlite3_bind_text(stmt, 1, key, -1, sqlite3_free);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
      *outIsEmpty = 1;
    } else {
      *outIsEmpty = 0;
      *outMedoid = sqlite3_column_int64(stmt, 0);
    }
    rc = SQLITE_OK;
  } else {
    rc = SQLITE_ERROR;
  }
  sqlite3_finalize(stmt);
  return rc;
}

/**
 * Set the medoid rowid for the given vector column's DiskANN index.
 * Pass isEmpty = 1 to mark the graph as empty (NULL medoid).
 */
static int diskann_medoid_set(vec0_vtab *p, int vec_col_idx,
                               i64 medoidRowid, int isEmpty) {
  int rc;
  sqlite3_stmt *stmt = NULL;
  char *key = sqlite3_mprintf("diskann_medoid_%02d", vec_col_idx);
  char *zSql = sqlite3_mprintf(
      "UPDATE " VEC0_SHADOW_INFO_NAME " SET value = ?2 WHERE key = ?1",
      p->schemaName, p->tableName);
  if (!key || !zSql) {
    sqlite3_free(key);
    sqlite3_free(zSql);
    return SQLITE_NOMEM;
  }

  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) {
    sqlite3_free(key);
    return rc;
  }

  sqlite3_bind_text(stmt, 1, key, -1, sqlite3_free);
  if (isEmpty) {
    sqlite3_bind_null(stmt, 2);
  } else {
    sqlite3_bind_int64(stmt, 2, medoidRowid);
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}


/**
 * Called when deleting a vector. If the deleted vector was the medoid,
 * pick a new one (the first available vector, or set to empty if none remain).
 */
static int diskann_medoid_handle_delete(vec0_vtab *p, int vec_col_idx,
                                          i64 deletedRowid) {
  i64 currentMedoid;
  int isEmpty;
  int rc = diskann_medoid_get(p, vec_col_idx, &currentMedoid, &isEmpty);
  if (rc != SQLITE_OK) return rc;

  if (!isEmpty && currentMedoid == deletedRowid) {
    sqlite3_stmt *stmt = NULL;
    char *zSql = sqlite3_mprintf(
        "SELECT rowid FROM " VEC0_SHADOW_VECTORS_N_NAME " WHERE rowid != ?1 LIMIT 1",
        p->schemaName, p->tableName, vec_col_idx);
    if (!zSql) return SQLITE_NOMEM;

    rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int64(stmt, 1, deletedRowid);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      i64 newMedoid = sqlite3_column_int64(stmt, 0);
      sqlite3_finalize(stmt);
      return diskann_medoid_set(p, vec_col_idx, newMedoid, 0);
    } else {
      sqlite3_finalize(stmt);
      return diskann_medoid_set(p, vec_col_idx, -1, 1);
    }
  }
  return SQLITE_OK;
}

// ============================================================
// DiskANN database I/O helpers
// ============================================================

/**
 * Read a node's full data from _diskann_nodes.
 * Returns blobs that must be freed by the caller with sqlite3_free().
 */
static int diskann_node_read(vec0_vtab *p, int vec_col_idx, i64 rowid,
                              u8 **outValidity, int *outValiditySize,
                              u8 **outNeighborIds, int *outNeighborIdsSize,
                              u8 **outQvecs, int *outQvecsSize) {
  int rc;
  if (!p->stmtDiskannNodeRead[vec_col_idx]) {
    char *zSql = sqlite3_mprintf(
        "SELECT neighbors_validity, neighbor_ids, neighbor_quantized_vectors "
        "FROM " VEC0_SHADOW_DISKANN_NODES_N_NAME " WHERE rowid = ?",
        p->schemaName, p->tableName, vec_col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1,
                             &p->stmtDiskannNodeRead[vec_col_idx], NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;
  }

  sqlite3_stmt *stmt = p->stmtDiskannNodeRead[vec_col_idx];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, 1, rowid);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    return SQLITE_ERROR;
  }

  int vs = sqlite3_column_bytes(stmt, 0);
  int is = sqlite3_column_bytes(stmt, 1);
  int qs = sqlite3_column_bytes(stmt, 2);

  // Validate blob sizes against config expectations to detect truncated /
  // corrupt data before any caller iterates using cfg->n_neighbors.
  {
    struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
    struct Vec0DiskannConfig *cfg = &col->diskann;
    int expectedVs = diskann_validity_byte_size(cfg->n_neighbors);
    int expectedIs = (int)diskann_neighbor_ids_byte_size(cfg->n_neighbors);
    int expectedQs = (int)diskann_neighbor_qvecs_byte_size(
        cfg->n_neighbors, cfg->quantizer_type, col->dimensions);
    if (vs != expectedVs || is != expectedIs || qs != expectedQs) {
      return SQLITE_CORRUPT;
    }
  }

  u8 *v = sqlite3_malloc(vs);
  u8 *ids = sqlite3_malloc(is);
  u8 *qv = sqlite3_malloc(qs);
  if (!v || !ids || !qv) {
    sqlite3_free(v);
    sqlite3_free(ids);
    sqlite3_free(qv);
    return SQLITE_NOMEM;
  }

  memcpy(v, sqlite3_column_blob(stmt, 0), vs);
  memcpy(ids, sqlite3_column_blob(stmt, 1), is);
  memcpy(qv, sqlite3_column_blob(stmt, 2), qs);

  *outValidity = v;       *outValiditySize = vs;
  *outNeighborIds = ids;  *outNeighborIdsSize = is;
  *outQvecs = qv;         *outQvecsSize = qs;
  return SQLITE_OK;
}

/**
 * Write (INSERT OR REPLACE) a node's data to _diskann_nodes.
 */
static int diskann_node_write(vec0_vtab *p, int vec_col_idx, i64 rowid,
                               const u8 *validity, int validitySize,
                               const u8 *neighborIds, int neighborIdsSize,
                               const u8 *qvecs, int qvecsSize) {
  int rc;
  if (!p->stmtDiskannNodeWrite[vec_col_idx]) {
    char *zSql = sqlite3_mprintf(
        "INSERT OR REPLACE INTO " VEC0_SHADOW_DISKANN_NODES_N_NAME
        " (rowid, neighbors_validity, neighbor_ids, neighbor_quantized_vectors) "
        "VALUES (?, ?, ?, ?)",
        p->schemaName, p->tableName, vec_col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1,
                             &p->stmtDiskannNodeWrite[vec_col_idx], NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;
  }

  sqlite3_stmt *stmt = p->stmtDiskannNodeWrite[vec_col_idx];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, 1, rowid);
  sqlite3_bind_blob(stmt, 2, validity, validitySize, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 3, neighborIds, neighborIdsSize, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 4, qvecs, qvecsSize, SQLITE_TRANSIENT);

  rc = sqlite3_step(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/**
 * Read the full-precision vector for a given rowid from _vectors.
 * Caller must free *outVector with sqlite3_free().
 */
static int diskann_vector_read(vec0_vtab *p, int vec_col_idx, i64 rowid,
                                void **outVector, int *outVectorSize) {
  int rc;
  if (!p->stmtVectorsRead[vec_col_idx]) {
    char *zSql = sqlite3_mprintf(
        "SELECT vector FROM " VEC0_SHADOW_VECTORS_N_NAME " WHERE rowid = ?",
        p->schemaName, p->tableName, vec_col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1,
                             &p->stmtVectorsRead[vec_col_idx], NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;
  }

  sqlite3_stmt *stmt = p->stmtVectorsRead[vec_col_idx];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, 1, rowid);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    return SQLITE_ERROR;
  }

  int sz = sqlite3_column_bytes(stmt, 0);
  void *vec = sqlite3_malloc(sz);
  if (!vec) return SQLITE_NOMEM;
  memcpy(vec, sqlite3_column_blob(stmt, 0), sz);

  *outVector = vec;
  *outVectorSize = sz;
  return SQLITE_OK;
}

/**
 * Write a full-precision vector to _vectors.
 */
static int diskann_vector_write(vec0_vtab *p, int vec_col_idx, i64 rowid,
                                 const void *vector, int vectorSize) {
  int rc;
  if (!p->stmtVectorsInsert[vec_col_idx]) {
    char *zSql = sqlite3_mprintf(
        "INSERT OR REPLACE INTO " VEC0_SHADOW_VECTORS_N_NAME
        " (rowid, vector) VALUES (?, ?)",
        p->schemaName, p->tableName, vec_col_idx);
    if (!zSql) return SQLITE_NOMEM;
    rc = sqlite3_prepare_v2(p->db, zSql, -1,
                             &p->stmtVectorsInsert[vec_col_idx], NULL);
    sqlite3_free(zSql);
    if (rc != SQLITE_OK) return rc;
  }

  sqlite3_stmt *stmt = p->stmtVectorsInsert[vec_col_idx];
  sqlite3_reset(stmt);
  sqlite3_bind_int64(stmt, 1, rowid);
  sqlite3_bind_blob(stmt, 2, vector, vectorSize, SQLITE_TRANSIENT);

  rc = sqlite3_step(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

// ============================================================
// DiskANN search data structures
// ============================================================

/**
 * A sorted candidate list for greedy beam search.
 */
struct DiskannCandidateList {
  struct Vec0DiskannCandidate *items;
  int count;
  int capacity;
};

static int diskann_candidate_list_init(struct DiskannCandidateList *list, int capacity) {
  list->items = sqlite3_malloc(capacity * sizeof(struct Vec0DiskannCandidate));
  if (!list->items) return SQLITE_NOMEM;
  list->count = 0;
  list->capacity = capacity;
  return SQLITE_OK;
}

static void diskann_candidate_list_free(struct DiskannCandidateList *list) {
  sqlite3_free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

/**
 * Insert a candidate into the sorted list, maintaining sort order by distance.
 * Deduplicates by rowid. If at capacity and new candidate is worse, discards it.
 * Returns 1 if inserted, 0 if discarded.
 */
static int diskann_candidate_list_insert(
    struct DiskannCandidateList *list, i64 rowid, f32 distance) {

  // Check for duplicate
  for (int i = 0; i < list->count; i++) {
    if (list->items[i].rowid == rowid) {
      // Update distance if better
      if (distance < list->items[i].distance) {
        list->items[i].distance = distance;
        // Re-sort this item into position
        struct Vec0DiskannCandidate tmp = list->items[i];
        int j = i - 1;
        while (j >= 0 && list->items[j].distance > tmp.distance) {
          list->items[j + 1] = list->items[j];
          j--;
        }
        list->items[j + 1] = tmp;
      }
      return 1;
    }
  }

  // If at capacity, check if new candidate is better than worst
  if (list->count >= list->capacity) {
    if (distance >= list->items[list->count - 1].distance) {
      return 0;  // Discard
    }
    list->count--;  // Make room by dropping the worst
  }

  // Binary search for insertion point
  int lo = 0, hi = list->count;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (list->items[mid].distance < distance) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  // Shift elements to make room
  memmove(&list->items[lo + 1], &list->items[lo],
          (list->count - lo) * sizeof(struct Vec0DiskannCandidate));

  list->items[lo].rowid = rowid;
  list->items[lo].distance = distance;
  list->items[lo].visited = 0;
  list->count++;
  return 1;
}

/**
 * Find the closest unvisited candidate. Returns its index, or -1 if none.
 */
static int diskann_candidate_list_next_unvisited(
    const struct DiskannCandidateList *list) {
  for (int i = 0; i < list->count; i++) {
    if (!list->items[i].visited) return i;
  }
  return -1;
}



/**
 * Simple hash set for tracking visited rowids during search.
 * Uses open addressing with linear probing.
 */
struct DiskannVisitedSet {
  i64 *slots;
  int capacity;
  int count;
};

static int diskann_visited_set_init(struct DiskannVisitedSet *set, int capacity) {
  // Round up to power of 2
  int cap = 16;
  while (cap < capacity) cap *= 2;
  set->slots = sqlite3_malloc(cap * sizeof(i64));
  if (!set->slots) return SQLITE_NOMEM;
  memset(set->slots, 0, cap * sizeof(i64));
  set->capacity = cap;
  set->count = 0;
  return SQLITE_OK;
}

static void diskann_visited_set_free(struct DiskannVisitedSet *set) {
  sqlite3_free(set->slots);
  set->slots = NULL;
  set->capacity = 0;
  set->count = 0;
}

static int diskann_visited_set_contains(const struct DiskannVisitedSet *set, i64 rowid) {
  if (rowid == 0) return 0;  // 0 is our sentinel for empty
  int mask = set->capacity - 1;
  int idx = (int)(((u64)rowid * 0x9E3779B97F4A7C15ULL) >> 32) & mask;
  for (int i = 0; i < set->capacity; i++) {
    int slot = (idx + i) & mask;
    if (set->slots[slot] == 0) return 0;
    if (set->slots[slot] == rowid) return 1;
  }
  return 0;
}

static int diskann_visited_set_insert(struct DiskannVisitedSet *set, i64 rowid) {
  if (rowid == 0) return 0;
  int mask = set->capacity - 1;
  int idx = (int)(((u64)rowid * 0x9E3779B97F4A7C15ULL) >> 32) & mask;
  for (int i = 0; i < set->capacity; i++) {
    int slot = (idx + i) & mask;
    if (set->slots[slot] == 0) {
      set->slots[slot] = rowid;
      set->count++;
      return 1;
    }
    if (set->slots[slot] == rowid) return 0;  // Already there
  }
  return 0;  // Full (shouldn't happen with proper sizing)
}

// ============================================================
// DiskANN greedy beam search (LM-Search)
// ============================================================

/**
 * Perform LM-Search: greedy beam search over the DiskANN graph.
 * Follows Algorithm 1 from the LM-DiskANN paper.
 */
static int diskann_search(
    vec0_vtab *p, int vec_col_idx,
    const void *queryVector, size_t dimensions,
    enum VectorElementType elementType,
    int k, int searchListSize,
    i64 *outRowids, f32 *outDistances, int *outCount) {

  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;

  if (searchListSize <= 0) {
    searchListSize = cfg->search_list_size_search > 0 ? cfg->search_list_size_search : cfg->search_list_size;
  }
  if (searchListSize < k) {
    searchListSize = k;
  }

  // 1. Get the medoid (entry point)
  i64 medoid;
  int isEmpty;
  rc = diskann_medoid_get(p, vec_col_idx, &medoid, &isEmpty);
  if (rc != SQLITE_OK) return rc;
  if (isEmpty) {
    *outCount = 0;
    return SQLITE_OK;
  }

  // 2. Compute distance from query to medoid using full-precision vector
  void *medoidVector = NULL;
  int medoidVectorSize;
  rc = diskann_vector_read(p, vec_col_idx, medoid, &medoidVector, &medoidVectorSize);
  if (rc != SQLITE_OK) return rc;

  f32 medoidDist = vec0_distance_full(queryVector, medoidVector,
                                          dimensions, elementType,
                                          col->distance_metric);
  sqlite3_free(medoidVector);

  // 3. Initialize candidate list and visited set
  struct DiskannCandidateList candidates;
  rc = diskann_candidate_list_init(&candidates, searchListSize);
  if (rc != SQLITE_OK) return rc;

  struct DiskannVisitedSet visited;
  rc = diskann_visited_set_init(&visited, searchListSize * 4);
  if (rc != SQLITE_OK) {
    diskann_candidate_list_free(&candidates);
    return rc;
  }

  // Seed with medoid
  diskann_candidate_list_insert(&candidates, medoid, medoidDist);

  // Pre-quantize query vector once for all quantized distance comparisons
  u8 *queryQuantized = NULL;
  if (elementType == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
    queryQuantized = diskann_quantize_query(
        (const f32 *)queryVector, dimensions, cfg->quantizer_type);
  }

  // 4. Greedy beam search loop (Algorithm 1 from LM-DiskANN paper)
  while (1) {
    int nextIdx = diskann_candidate_list_next_unvisited(&candidates);
    if (nextIdx < 0) break;

    struct Vec0DiskannCandidate *current = &candidates.items[nextIdx];
    current->visited = 1;
    i64 currentRowid = current->rowid;

    // Read the node's neighbor data
    u8 *validity = NULL, *neighborIds = NULL, *qvecs = NULL;
    int validitySize, neighborIdsSize, qvecsSize;
    rc = diskann_node_read(p, vec_col_idx, currentRowid,
                            &validity, &validitySize,
                            &neighborIds, &neighborIdsSize,
                            &qvecs, &qvecsSize);
    if (rc != SQLITE_OK) {
      continue;  // Skip if node doesn't exist
    }

    // Insert all valid neighbors with approximate (quantized) distances
    for (int i = 0; i < cfg->n_neighbors; i++) {
      if (!diskann_validity_get(validity, i)) continue;

      i64 neighborRowid = diskann_neighbor_id_get(neighborIds, i);

      if (diskann_visited_set_contains(&visited, neighborRowid)) continue;

      const u8 *neighborQvec = diskann_neighbor_qvec_get(
          qvecs, i, cfg->quantizer_type, dimensions);

      f32 approxDist;
      if (queryQuantized) {
        approxDist = diskann_distance_quantized_precomputed(
            queryQuantized, neighborQvec, dimensions,
            cfg->quantizer_type, col->distance_metric);
      } else {
        approxDist = diskann_distance_quantized(
            queryVector, neighborQvec, dimensions,
            cfg->quantizer_type, col->distance_metric);
      }

      diskann_candidate_list_insert(&candidates, neighborRowid, approxDist);
    }

    sqlite3_free(validity);
    sqlite3_free(neighborIds);
    sqlite3_free(qvecs);

    // Add to visited set
    diskann_visited_set_insert(&visited, currentRowid);

    // Paper line 13: Re-rank p* using full-precision distance
    // We already have exact distance for medoid; for others, update now
    void *fullVec = NULL;
    int fullVecSize;
    rc = diskann_vector_read(p, vec_col_idx, currentRowid, &fullVec, &fullVecSize);
    if (rc == SQLITE_OK) {
      f32 exactDist = vec0_distance_full(queryVector, fullVec,
                                             dimensions, elementType,
                                             col->distance_metric);
      sqlite3_free(fullVec);
      // Update distance in candidate list and re-sort
      diskann_candidate_list_insert(&candidates, currentRowid, exactDist);
    }
  }

  // 5. Output results (candidates are already sorted by distance)
  int resultCount = (candidates.count < k) ? candidates.count : k;
  *outCount = resultCount;
  for (int i = 0; i < resultCount; i++) {
    outRowids[i] = candidates.items[i].rowid;
    outDistances[i] = candidates.items[i].distance;
  }

  sqlite3_free(queryQuantized);
  diskann_candidate_list_free(&candidates);
  diskann_visited_set_free(&visited);
  return SQLITE_OK;
}

// ============================================================
// DiskANN RobustPrune (Algorithm 4 from LM-DiskANN paper)
// ============================================================

/**
 * RobustPrune: Select up to max_neighbors neighbors for node p from a
 * candidate set, using alpha-pruning for diversity.
 *
 * Following Algorithm 4 (LM-Prune):
 *   C = C union N_out(p) \ {p}
 *   N_out(p) = empty
 *   while C not empty:
 *     p* = argmin d(p, c) for c in C
 *     N_out(p).insert(p*)
 *     if |N_out(p)| >= R: break
 *     for each p' in C:
 *       if alpha * d(p*, p') <= d(p, p'): remove p' from C
 */
/**
 * Pure function: given pre-sorted candidates and a distance matrix, select
 * up to max_neighbors using alpha-pruning. inter_distances is a flattened
 * num_candidates x num_candidates matrix where inter_distances[i*num_candidates+j]
 * = d(candidate_i, candidate_j). p_distances[i] = d(p, candidate_i), already sorted.
 * outSelected[i] = 1 if selected. Returns count of selected.
 */
int diskann_prune_select(
    const f32 *inter_distances, const f32 *p_distances,
    int num_candidates, f32 alpha, int max_neighbors,
    int *outSelected, int *outCount) {

  if (num_candidates == 0) {
    *outCount = 0;
    return SQLITE_OK;
  }

  u8 *active = sqlite3_malloc(num_candidates);
  if (!active) return SQLITE_NOMEM;
  memset(active, 1, num_candidates);
  memset(outSelected, 0, num_candidates * sizeof(int));

  int selectedCount = 0;

  for (int round = 0; round < num_candidates && selectedCount < max_neighbors; round++) {
    int bestIdx = -1;
    for (int i = 0; i < num_candidates; i++) {
      if (active[i]) { bestIdx = i; break; }
    }
    if (bestIdx < 0) break;

    outSelected[bestIdx] = 1;
    selectedCount++;
    active[bestIdx] = 0;

    for (int i = 0; i < num_candidates; i++) {
      if (!active[i]) continue;
      f32 dist_best_to_i = inter_distances[bestIdx * num_candidates + i];
      if (alpha * dist_best_to_i <= p_distances[i]) {
        active[i] = 0;
      }
    }
  }

  *outCount = selectedCount;
  sqlite3_free(active);
  return SQLITE_OK;
}

static int diskann_robust_prune(
    vec0_vtab *p, int vec_col_idx,
    i64 p_rowid, const void *p_vector,
    i64 *candidates, f32 *candidate_distances, int num_candidates,
    f32 alpha, int max_neighbors,
    i64 *outNeighborRowids, int *outNeighborCount) {

  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  int rc;

  // Remove p itself from candidates
  for (int i = 0; i < num_candidates; i++) {
    if (candidates[i] == p_rowid) {
      candidates[i] = candidates[num_candidates - 1];
      candidate_distances[i] = candidate_distances[num_candidates - 1];
      num_candidates--;
      break;
    }
  }

  if (num_candidates == 0) {
    *outNeighborCount = 0;
    return SQLITE_OK;
  }

  // Sort candidates by distance to p (ascending) - insertion sort
  for (int i = 1; i < num_candidates; i++) {
    f32 tmpDist = candidate_distances[i];
    i64 tmpRowid = candidates[i];
    int j = i - 1;
    while (j >= 0 && candidate_distances[j] > tmpDist) {
      candidate_distances[j + 1] = candidate_distances[j];
      candidates[j + 1] = candidates[j];
      j--;
    }
    candidate_distances[j + 1] = tmpDist;
    candidates[j + 1] = tmpRowid;
  }

  // Active flags
  u8 *active = sqlite3_malloc(num_candidates);
  if (!active) return SQLITE_NOMEM;
  memset(active, 1, num_candidates);

  // Cache full-precision vectors for inter-candidate distance
  void **candidateVectors = sqlite3_malloc(num_candidates * sizeof(void *));
  if (!candidateVectors) {
    sqlite3_free(active);
    return SQLITE_NOMEM;
  }
  memset(candidateVectors, 0, num_candidates * sizeof(void *));

  int selectedCount = 0;

  for (int round = 0; round < num_candidates && selectedCount < max_neighbors; round++) {
    // Find closest active candidate
    int bestIdx = -1;
    for (int i = 0; i < num_candidates; i++) {
      if (active[i]) { bestIdx = i; break; }
    }
    if (bestIdx < 0) break;

    // Select this candidate
    outNeighborRowids[selectedCount] = candidates[bestIdx];
    selectedCount++;
    active[bestIdx] = 0;

    // Load selected candidate's vector
    if (!candidateVectors[bestIdx]) {
      int vecSize;
      rc = diskann_vector_read(p, vec_col_idx, candidates[bestIdx],
                                &candidateVectors[bestIdx], &vecSize);
      if (rc != SQLITE_OK) continue;
    }

    // Alpha-prune: remove candidates covered by the selected neighbor
    for (int i = 0; i < num_candidates; i++) {
      if (!active[i]) continue;

      if (!candidateVectors[i]) {
        int vecSize;
        rc = diskann_vector_read(p, vec_col_idx, candidates[i],
                                  &candidateVectors[i], &vecSize);
        if (rc != SQLITE_OK) continue;
      }

      f32 dist_selected_to_i = vec0_distance_full(
          candidateVectors[bestIdx], candidateVectors[i],
          col->dimensions, col->element_type, col->distance_metric);

      if (alpha * dist_selected_to_i <= candidate_distances[i]) {
        active[i] = 0;
      }
    }
  }

  *outNeighborCount = selectedCount;

  for (int i = 0; i < num_candidates; i++) {
    sqlite3_free(candidateVectors[i]);
  }
  sqlite3_free(candidateVectors);
  sqlite3_free(active);

  return SQLITE_OK;
}

/**
 * After RobustPrune selects neighbors, build the node blobs and write to DB.
 * Quantizes each neighbor's vector and packs into the node format.
 */
static int diskann_write_pruned_neighbors(
    vec0_vtab *p, int vec_col_idx, i64 nodeRowid,
    const i64 *neighborRowids, int neighborCount) {

  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;

  u8 *validity, *neighborIds, *qvecs;
  int validitySize, neighborIdsSize, qvecsSize;
  rc = diskann_node_init(cfg->n_neighbors, cfg->quantizer_type,
                          col->dimensions,
                          &validity, &validitySize,
                          &neighborIds, &neighborIdsSize,
                          &qvecs, &qvecsSize);
  if (rc != SQLITE_OK) return rc;

  size_t qvecSize = diskann_quantized_vector_byte_size(
      cfg->quantizer_type, col->dimensions);
  u8 *qvec = sqlite3_malloc(qvecSize);
  if (!qvec) {
    sqlite3_free(validity);
    sqlite3_free(neighborIds);
    sqlite3_free(qvecs);
    return SQLITE_NOMEM;
  }

  for (int i = 0; i < neighborCount && i < cfg->n_neighbors; i++) {
    void *neighborVec = NULL;
    int neighborVecSize;
    rc = diskann_vector_read(p, vec_col_idx, neighborRowids[i],
                              &neighborVec, &neighborVecSize);
    if (rc != SQLITE_OK) continue;

    if (col->element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
      diskann_quantize_vector((const f32 *)neighborVec, col->dimensions,
                               cfg->quantizer_type, qvec);
    } else {
      memcpy(qvec, neighborVec,
             qvecSize < (size_t)neighborVecSize ? qvecSize : (size_t)neighborVecSize);
    }

    diskann_node_set_neighbor(validity, neighborIds, qvecs, i,
                               neighborRowids[i], qvec,
                               cfg->quantizer_type, col->dimensions);

    sqlite3_free(neighborVec);
  }
  sqlite3_free(qvec);

  rc = diskann_node_write(p, vec_col_idx, nodeRowid,
                           validity, validitySize,
                           neighborIds, neighborIdsSize,
                           qvecs, qvecsSize);

  sqlite3_free(validity);
  sqlite3_free(neighborIds);
  sqlite3_free(qvecs);
  return rc;
}

// ============================================================
// DiskANN insert (Algorithm 2 from LM-DiskANN paper)
// ============================================================

/**
 * Add a reverse edge: make target_rowid a neighbor of node_rowid.
 * If node is full, run RobustPrune.
 */
static int diskann_add_reverse_edge(
    vec0_vtab *p, int vec_col_idx,
    i64 node_rowid, i64 target_rowid, const void *target_vector) {

  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;

  u8 *validity = NULL, *neighborIds = NULL, *qvecs = NULL;
  int validitySize, neighborIdsSize, qvecsSize;
  rc = diskann_node_read(p, vec_col_idx, node_rowid,
                          &validity, &validitySize,
                          &neighborIds, &neighborIdsSize,
                          &qvecs, &qvecsSize);
  if (rc != SQLITE_OK) return rc;

  int currentCount = diskann_validity_count(validity, cfg->n_neighbors);

  // Check if target is already a neighbor
  for (int i = 0; i < cfg->n_neighbors; i++) {
    if (diskann_validity_get(validity, i) &&
        diskann_neighbor_id_get(neighborIds, i) == target_rowid) {
      sqlite3_free(validity);
      sqlite3_free(neighborIds);
      sqlite3_free(qvecs);
      return SQLITE_OK;
    }
  }

  if (currentCount < cfg->n_neighbors) {
    // Room available: find first empty slot
    for (int i = 0; i < cfg->n_neighbors; i++) {
      if (!diskann_validity_get(validity, i)) {
        size_t qvecSize = diskann_quantized_vector_byte_size(
            cfg->quantizer_type, col->dimensions);
        u8 *qvec = sqlite3_malloc(qvecSize);
        if (!qvec) {
          sqlite3_free(validity);
          sqlite3_free(neighborIds);
          sqlite3_free(qvecs);
          return SQLITE_NOMEM;
        }

        if (col->element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
          diskann_quantize_vector((const f32 *)target_vector, col->dimensions,
                                   cfg->quantizer_type, qvec);
        } else {
          size_t vbs = vector_column_byte_size(*col);
          memcpy(qvec, target_vector, qvecSize < vbs ? qvecSize : vbs);
        }

        diskann_node_set_neighbor(validity, neighborIds, qvecs, i,
                                   target_rowid, qvec,
                                   cfg->quantizer_type, col->dimensions);
        sqlite3_free(qvec);
        break;
      }
    }

    rc = diskann_node_write(p, vec_col_idx, node_rowid,
                             validity, validitySize,
                             neighborIds, neighborIdsSize,
                             qvecs, qvecsSize);
  } else {
    // Full: lazy replacement — use quantized distances to find the worst
    // existing neighbor and replace it if target is closer. This avoids
    // reading all neighbors' float vectors (the expensive RobustPrune path).

    // Quantize the node's vector and the target vector for comparison
    void *nodeVector = NULL;
    int nodeVecSize;
    rc = diskann_vector_read(p, vec_col_idx, node_rowid,
                              &nodeVector, &nodeVecSize);
    if (rc != SQLITE_OK) {
      sqlite3_free(validity);
      sqlite3_free(neighborIds);
      sqlite3_free(qvecs);
      return rc;
    }

    // Quantize target for node-level comparison
    size_t qvecSize = diskann_quantized_vector_byte_size(
        cfg->quantizer_type, col->dimensions);
    u8 *targetQ = sqlite3_malloc(qvecSize);
    u8 *nodeQ = sqlite3_malloc(qvecSize);
    if (!targetQ || !nodeQ) {
      sqlite3_free(targetQ);
      sqlite3_free(nodeQ);
      sqlite3_free(nodeVector);
      sqlite3_free(validity);
      sqlite3_free(neighborIds);
      sqlite3_free(qvecs);
      return SQLITE_NOMEM;
    }

    if (col->element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
      diskann_quantize_vector((const f32 *)target_vector, col->dimensions,
                               cfg->quantizer_type, targetQ);
      diskann_quantize_vector((const f32 *)nodeVector, col->dimensions,
                               cfg->quantizer_type, nodeQ);
    } else {
      memcpy(targetQ, target_vector, qvecSize);
      memcpy(nodeQ, nodeVector, qvecSize);
    }

    // Compute quantized distance from node to target
    f32 targetDist = diskann_distance_quantized_precomputed(
        nodeQ, targetQ, col->dimensions,
        cfg->quantizer_type, col->distance_metric);

    // Find the worst (farthest) existing neighbor using quantized distances
    int worstIdx = -1;
    f32 worstDist = -1.0f;
    for (int i = 0; i < cfg->n_neighbors; i++) {
      if (!diskann_validity_get(validity, i)) continue;
      const u8 *nqvec = diskann_neighbor_qvec_get(
          qvecs, i, cfg->quantizer_type, col->dimensions);
      f32 d = diskann_distance_quantized_precomputed(
          nodeQ, nqvec, col->dimensions,
          cfg->quantizer_type, col->distance_metric);
      if (d > worstDist) {
        worstDist = d;
        worstIdx = i;
      }
    }

    // Replace worst neighbor if target is closer
    if (worstIdx >= 0 && targetDist < worstDist) {
      diskann_node_set_neighbor(validity, neighborIds, qvecs, worstIdx,
                                 target_rowid, targetQ,
                                 cfg->quantizer_type, col->dimensions);
      rc = diskann_node_write(p, vec_col_idx, node_rowid,
                               validity, validitySize,
                               neighborIds, neighborIdsSize,
                               qvecs, qvecsSize);
    } else {
      rc = SQLITE_OK;  // target is farther than all existing neighbors, skip
    }

    sqlite3_free(targetQ);
    sqlite3_free(nodeQ);
    sqlite3_free(nodeVector);
  }

  sqlite3_free(validity);
  sqlite3_free(neighborIds);
  sqlite3_free(qvecs);
  return rc;
}

// ============================================================
// DiskANN buffer helpers (for batched inserts)
// ============================================================

/**
 * Insert a vector into the _diskann_buffer table.
 */
static int diskann_buffer_write(vec0_vtab *p, int vec_col_idx,
                                 i64 rowid, const void *vector, int vectorSize) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "INSERT INTO " VEC0_SHADOW_DISKANN_BUFFER_N_NAME
      " (rowid, vector) VALUES (?, ?)",
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int64(stmt, 1, rowid);
  sqlite3_bind_blob(stmt, 2, vector, vectorSize, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/**
 * Delete a vector from the _diskann_buffer table.
 */
static int diskann_buffer_delete(vec0_vtab *p, int vec_col_idx, i64 rowid) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "DELETE FROM " VEC0_SHADOW_DISKANN_BUFFER_N_NAME " WHERE rowid = ?",
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int64(stmt, 1, rowid);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/**
 * Check if a rowid exists in the _diskann_buffer table.
 * Returns SQLITE_OK and sets *exists to 1 if found, 0 if not.
 */
static int diskann_buffer_exists(vec0_vtab *p, int vec_col_idx,
                                  i64 rowid, int *exists) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "SELECT 1 FROM " VEC0_SHADOW_DISKANN_BUFFER_N_NAME " WHERE rowid = ?",
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int64(stmt, 1, rowid);
  rc = sqlite3_step(stmt);
  *exists = (rc == SQLITE_ROW) ? 1 : 0;
  sqlite3_finalize(stmt);
  return SQLITE_OK;
}

/**
 * Get the count of rows in the _diskann_buffer table.
 */
static int diskann_buffer_count(vec0_vtab *p, int vec_col_idx, i64 *count) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "SELECT count(*) FROM " VEC0_SHADOW_DISKANN_BUFFER_N_NAME,
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return SQLITE_OK;
  }
  sqlite3_finalize(stmt);
  return SQLITE_ERROR;
}

// Forward declaration: diskann_insert_graph does the actual graph insertion
static int diskann_insert_graph(vec0_vtab *p, int vec_col_idx,
                                 i64 rowid, const void *vector);

/**
 * Flush all buffered vectors into the DiskANN graph.
 * Iterates over _diskann_buffer rows and calls diskann_insert_graph for each.
 */
static int diskann_flush_buffer(vec0_vtab *p, int vec_col_idx) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "SELECT rowid, vector FROM " VEC0_SHADOW_DISKANN_BUFFER_N_NAME,
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    i64 rowid = sqlite3_column_int64(stmt, 0);
    const void *vector = sqlite3_column_blob(stmt, 1);
    // Note: vector is already written to _vectors table, so
    // diskann_insert_graph will skip re-writing it (vector already exists).
    // We call the graph-only insert path.
    int insertRc = diskann_insert_graph(p, vec_col_idx, rowid, vector);
    if (insertRc != SQLITE_OK) {
      sqlite3_finalize(stmt);
      return insertRc;
    }
  }
  sqlite3_finalize(stmt);

  // Clear the buffer
  zSql = sqlite3_mprintf(
      "DELETE FROM " VEC0_SHADOW_DISKANN_BUFFER_N_NAME,
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/**
 * Insert a new vector into the DiskANN graph (graph-only path).
 * The vector must already be written to _vectors table.
 * This is the core graph insertion logic (Algorithm 2: LM-Insert).
 */
static int diskann_insert_graph(vec0_vtab *p, int vec_col_idx,
                                 i64 rowid, const void *vector) {
  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;

  // Handle first insert (empty graph)
  i64 medoid;
  int isEmpty;
  rc = diskann_medoid_get(p, vec_col_idx, &medoid, &isEmpty);
  if (rc != SQLITE_OK) return rc;

  if (isEmpty) {
    u8 *validity, *neighborIds, *qvecs;
    int validitySize, neighborIdsSize, qvecsSize;
    rc = diskann_node_init(cfg->n_neighbors, cfg->quantizer_type,
                            col->dimensions,
                            &validity, &validitySize,
                            &neighborIds, &neighborIdsSize,
                            &qvecs, &qvecsSize);
    if (rc != SQLITE_OK) return rc;

    rc = diskann_node_write(p, vec_col_idx, rowid,
                             validity, validitySize,
                             neighborIds, neighborIdsSize,
                             qvecs, qvecsSize);
    sqlite3_free(validity);
    sqlite3_free(neighborIds);
    sqlite3_free(qvecs);
    if (rc != SQLITE_OK) return rc;

    return diskann_medoid_set(p, vec_col_idx, rowid, 0);
  }

  // Search for nearest neighbors
  int L = cfg->search_list_size_insert > 0 ? cfg->search_list_size_insert : cfg->search_list_size;
  i64 *searchRowids = sqlite3_malloc(L * sizeof(i64));
  f32 *searchDistances = sqlite3_malloc(L * sizeof(f32));
  if (!searchRowids || !searchDistances) {
    sqlite3_free(searchRowids);
    sqlite3_free(searchDistances);
    return SQLITE_NOMEM;
  }

  int searchCount;
  rc = diskann_search(p, vec_col_idx, vector, col->dimensions,
                       col->element_type, L, L,
                       searchRowids, searchDistances, &searchCount);
  if (rc != SQLITE_OK) {
    sqlite3_free(searchRowids);
    sqlite3_free(searchDistances);
    return rc;
  }

  // RobustPrune to select neighbors for x
  i64 *selectedNeighbors = sqlite3_malloc(cfg->n_neighbors * sizeof(i64));
  int selectedCount = 0;
  if (!selectedNeighbors) {
    sqlite3_free(searchRowids);
    sqlite3_free(searchDistances);
    return SQLITE_NOMEM;
  }

  rc = diskann_robust_prune(p, vec_col_idx, rowid, vector,
                             searchRowids, searchDistances, searchCount,
                             cfg->alpha, cfg->n_neighbors,
                             selectedNeighbors, &selectedCount);
  sqlite3_free(searchRowids);
  sqlite3_free(searchDistances);
  if (rc != SQLITE_OK) {
    sqlite3_free(selectedNeighbors);
    return rc;
  }

  // Write x's node with selected neighbors
  rc = diskann_write_pruned_neighbors(p, vec_col_idx, rowid,
                                       selectedNeighbors, selectedCount);
  if (rc != SQLITE_OK) {
    sqlite3_free(selectedNeighbors);
    return rc;
  }

  // Add bidirectional edges
  for (int i = 0; i < selectedCount; i++) {
    diskann_add_reverse_edge(p, vec_col_idx,
                              selectedNeighbors[i], rowid, vector);
  }

  sqlite3_free(selectedNeighbors);
  return SQLITE_OK;
}

/**
 * Insert a new vector into the DiskANN index (Algorithm 2: LM-Insert).
 * When buffer_threshold > 0, vectors are buffered and flushed in batch.
 */
static int diskann_insert(vec0_vtab *p, int vec_col_idx,
                           i64 rowid, const void *vector) {
  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;
  size_t vectorSize = vector_column_byte_size(*col);

  // 1. Write full-precision vector to _vectors table (always needed for queries)
  rc = diskann_vector_write(p, vec_col_idx, rowid, vector, (int)vectorSize);
  if (rc != SQLITE_OK) return rc;

  // 2. If buffering is enabled, write to buffer instead of graph
  if (cfg->buffer_threshold > 0) {
    rc = diskann_buffer_write(p, vec_col_idx, rowid, vector, (int)vectorSize);
    if (rc != SQLITE_OK) return rc;

    i64 count;
    rc = diskann_buffer_count(p, vec_col_idx, &count);
    if (rc != SQLITE_OK) return rc;

    if (count >= cfg->buffer_threshold) {
      return diskann_flush_buffer(p, vec_col_idx);
    }
    return SQLITE_OK;
  }

  // 3. Legacy per-row insert directly into graph
  return diskann_insert_graph(p, vec_col_idx, rowid, vector);
}

/**
 * Returns 1 if ALL vector columns in this table are DiskANN-indexed.
 */
// ============================================================
// DiskANN delete (Algorithm 3 from LM-DiskANN paper)
// ============================================================

static int diskann_node_delete(vec0_vtab *p, int vec_col_idx, i64 rowid) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "DELETE FROM " VEC0_SHADOW_DISKANN_NODES_N_NAME " WHERE rowid = ?",
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int64(stmt, 1, rowid);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

static int diskann_vector_delete(vec0_vtab *p, int vec_col_idx, i64 rowid) {
  sqlite3_stmt *stmt = NULL;
  char *zSql = sqlite3_mprintf(
      "DELETE FROM " VEC0_SHADOW_VECTORS_N_NAME " WHERE rowid = ?",
      p->schemaName, p->tableName, vec_col_idx);
  if (!zSql) return SQLITE_NOMEM;
  int rc = sqlite3_prepare_v2(p->db, zSql, -1, &stmt, NULL);
  sqlite3_free(zSql);
  if (rc != SQLITE_OK) return rc;
  sqlite3_bind_int64(stmt, 1, rowid);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/**
 * Repair graph after deleting a node. Following Algorithm 3 (LM-Delete):
 * For each neighbor n of the deleted node, add deleted node's other neighbors
 * to n's candidate set, then remove the deleted node from n's neighbor list.
 * Uses simple slot replacement rather than full RobustPrune for performance.
 */
static int diskann_repair_reverse_edges(
    vec0_vtab *p, int vec_col_idx, i64 deleted_rowid,
    const i64 *deleted_neighbors, int deleted_neighbor_count) {

  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;

  // For each neighbor of the deleted node, fix their neighbor list
  for (int dn = 0; dn < deleted_neighbor_count; dn++) {
    i64 nodeRowid = deleted_neighbors[dn];

    u8 *validity = NULL, *neighborIds = NULL, *qvecs = NULL;
    int vs, nis, qs;
    rc = diskann_node_read(p, vec_col_idx, nodeRowid,
                            &validity, &vs, &neighborIds, &nis, &qvecs, &qs);
    if (rc != SQLITE_OK) continue;

    // Find and clear the deleted node's slot
    int clearedSlot = -1;
    for (int i = 0; i < cfg->n_neighbors; i++) {
      if (diskann_validity_get(validity, i) &&
          diskann_neighbor_id_get(neighborIds, i) == deleted_rowid) {
        diskann_node_clear_neighbor(validity, neighborIds, qvecs, i,
                                     cfg->quantizer_type, col->dimensions);
        clearedSlot = i;
        break;
      }
    }

    if (clearedSlot >= 0) {
      // Try to fill the cleared slot with one of the deleted node's other neighbors
      for (int di = 0; di < deleted_neighbor_count; di++) {
        i64 candidate = deleted_neighbors[di];
        if (candidate == nodeRowid || candidate == deleted_rowid) continue;

        // Check not already a neighbor
        int alreadyNeighbor = 0;
        for (int ni = 0; ni < cfg->n_neighbors; ni++) {
          if (diskann_validity_get(validity, ni) &&
              diskann_neighbor_id_get(neighborIds, ni) == candidate) {
            alreadyNeighbor = 1;
            break;
          }
        }
        if (alreadyNeighbor) continue;

        // Load, quantize, and set
        void *candidateVec = NULL;
        int cvs;
        rc = diskann_vector_read(p, vec_col_idx, candidate, &candidateVec, &cvs);
        if (rc != SQLITE_OK) continue;

        size_t qvecSize = diskann_quantized_vector_byte_size(
            cfg->quantizer_type, col->dimensions);
        u8 *qvec = sqlite3_malloc(qvecSize);
        if (qvec) {
          if (col->element_type == SQLITE_VEC_ELEMENT_TYPE_FLOAT32) {
            diskann_quantize_vector((const f32 *)candidateVec, col->dimensions,
                                     cfg->quantizer_type, qvec);
          } else {
            memcpy(qvec, candidateVec,
                   qvecSize < (size_t)cvs ? qvecSize : (size_t)cvs);
          }
          diskann_node_set_neighbor(validity, neighborIds, qvecs, clearedSlot,
                                     candidate, qvec,
                                     cfg->quantizer_type, col->dimensions);
          sqlite3_free(qvec);
        }
        sqlite3_free(candidateVec);
        break;
      }

      diskann_node_write(p, vec_col_idx, nodeRowid,
                          validity, vs, neighborIds, nis, qvecs, qs);
    }

    sqlite3_free(validity);
    sqlite3_free(neighborIds);
    sqlite3_free(qvecs);
  }

  return SQLITE_OK;
}

/**
 * Delete a vector from the DiskANN graph (Algorithm 3: LM-Delete).
 * If the vector is in the buffer (not yet flushed), just remove from buffer.
 */
static int diskann_delete(vec0_vtab *p, int vec_col_idx, i64 rowid) {
  struct VectorColumnDefinition *col = &p->vector_columns[vec_col_idx];
  struct Vec0DiskannConfig *cfg = &col->diskann;
  int rc;

  // Check if this rowid is in the buffer (not yet in graph)
  if (cfg->buffer_threshold > 0) {
    int inBuffer = 0;
    rc = diskann_buffer_exists(p, vec_col_idx, rowid, &inBuffer);
    if (rc != SQLITE_OK) return rc;
    if (inBuffer) {
      // Just remove from buffer and _vectors, no graph repair needed
      rc = diskann_buffer_delete(p, vec_col_idx, rowid);
      if (rc == SQLITE_OK) {
        rc = diskann_vector_delete(p, vec_col_idx, rowid);
      }
      return rc;
    }
  }

  // 1. Read the node to get its neighbor list
  u8 *delValidity = NULL, *delNeighborIds = NULL, *delQvecs = NULL;
  int dvs, dnis, dqs;
  rc = diskann_node_read(p, vec_col_idx, rowid,
                          &delValidity, &dvs, &delNeighborIds, &dnis,
                          &delQvecs, &dqs);
  if (rc != SQLITE_OK) {
    return SQLITE_OK;  // Node doesn't exist, nothing to do
  }

  i64 *deletedNeighbors = sqlite3_malloc(cfg->n_neighbors * sizeof(i64));
  int deletedNeighborCount = 0;
  if (!deletedNeighbors) {
    sqlite3_free(delValidity);
    sqlite3_free(delNeighborIds);
    sqlite3_free(delQvecs);
    return SQLITE_NOMEM;
  }

  for (int i = 0; i < cfg->n_neighbors; i++) {
    if (diskann_validity_get(delValidity, i)) {
      deletedNeighbors[deletedNeighborCount++] =
          diskann_neighbor_id_get(delNeighborIds, i);
    }
  }

  sqlite3_free(delValidity);
  sqlite3_free(delNeighborIds);
  sqlite3_free(delQvecs);

  // 2. Repair reverse edges
  rc = diskann_repair_reverse_edges(p, vec_col_idx, rowid,
                                     deletedNeighbors, deletedNeighborCount);
  sqlite3_free(deletedNeighbors);

  // 3. Delete node and vector
  if (rc == SQLITE_OK) {
    rc = diskann_node_delete(p, vec_col_idx, rowid);
  }
  if (rc == SQLITE_OK) {
    rc = diskann_vector_delete(p, vec_col_idx, rowid);
  }

  // 4. Handle medoid deletion
  if (rc == SQLITE_OK) {
    rc = diskann_medoid_handle_delete(p, vec_col_idx, rowid);
  }

  return rc;
}

static int vec0_all_columns_diskann(vec0_vtab *p) {
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type != VEC0_INDEX_TYPE_DISKANN) return 0;
  }
  return p->numVectorColumns > 0;
}

// ============================================================================
// Command dispatch
// ============================================================================

static int diskann_handle_command(vec0_vtab *p, const char *command) {
  int col_idx = -1;
  for (int i = 0; i < p->numVectorColumns; i++) {
    if (p->vector_columns[i].index_type == VEC0_INDEX_TYPE_DISKANN) { col_idx = i; break; }
  }
  if (col_idx < 0) return SQLITE_EMPTY;

  struct Vec0DiskannConfig *cfg = &p->vector_columns[col_idx].diskann;

  if (strncmp(command, "search_list_size_search=", 24) == 0) {
    int val = atoi(command + 24);
    if (val < 1) { vtab_set_error(&p->base, "search_list_size_search must be >= 1"); return SQLITE_ERROR; }
    cfg->search_list_size_search = val;
    return SQLITE_OK;
  }
  if (strncmp(command, "search_list_size_insert=", 24) == 0) {
    int val = atoi(command + 24);
    if (val < 1) { vtab_set_error(&p->base, "search_list_size_insert must be >= 1"); return SQLITE_ERROR; }
    cfg->search_list_size_insert = val;
    return SQLITE_OK;
  }
  if (strncmp(command, "search_list_size=", 17) == 0) {
    int val = atoi(command + 17);
    if (val < 1) { vtab_set_error(&p->base, "search_list_size must be >= 1"); return SQLITE_ERROR; }
    cfg->search_list_size = val;
    return SQLITE_OK;
  }
  return SQLITE_EMPTY;
}

#ifdef SQLITE_VEC_TEST
// Expose internal DiskANN data structures and functions for unit testing.

int _test_diskann_candidate_list_init(struct DiskannCandidateList *list, int capacity) {
  return diskann_candidate_list_init(list, capacity);
}
void _test_diskann_candidate_list_free(struct DiskannCandidateList *list) {
  diskann_candidate_list_free(list);
}
int _test_diskann_candidate_list_insert(struct DiskannCandidateList *list, long long rowid, float distance) {
  return diskann_candidate_list_insert(list, (i64)rowid, (f32)distance);
}
int _test_diskann_candidate_list_next_unvisited(const struct DiskannCandidateList *list) {
  return diskann_candidate_list_next_unvisited(list);
}
int _test_diskann_candidate_list_count(const struct DiskannCandidateList *list) {
  return list->count;
}
long long _test_diskann_candidate_list_rowid(const struct DiskannCandidateList *list, int i) {
  return (long long)list->items[i].rowid;
}
float _test_diskann_candidate_list_distance(const struct DiskannCandidateList *list, int i) {
  return (float)list->items[i].distance;
}
void _test_diskann_candidate_list_set_visited(struct DiskannCandidateList *list, int i) {
  list->items[i].visited = 1;
}

int _test_diskann_visited_set_init(struct DiskannVisitedSet *set, int capacity) {
  return diskann_visited_set_init(set, capacity);
}
void _test_diskann_visited_set_free(struct DiskannVisitedSet *set) {
  diskann_visited_set_free(set);
}
int _test_diskann_visited_set_contains(const struct DiskannVisitedSet *set, long long rowid) {
  return diskann_visited_set_contains(set, (i64)rowid);
}
int _test_diskann_visited_set_insert(struct DiskannVisitedSet *set, long long rowid) {
  return diskann_visited_set_insert(set, (i64)rowid);
}
#endif /* SQLITE_VEC_TEST */

