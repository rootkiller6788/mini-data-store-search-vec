#include "vector_db.h"
#include "serialization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* L6: Vector Database — Canonical Problem: Full-Featured Vector Store
 *
 * This implements a complete vector database with:
 * - Collection management (CREATE/DROP — DDL)
 * - Data operations (INSERT/SEARCH/DELETE — DML)
 * - Index lifecycle (BUILD/TRAIN)
 * - Persistence (SAVE/LOAD)
 *
 * Comparable to a simplified FAISS IndexIDMap + persistence layer.
 * Each collection wraps one of four index types selected at creation.
 */

/* === Initialization (L2: Core Concept — database lifecycle) === */

void vdb_init(VectorDB *db) {
    memset(db, 0, sizeof(*db));
    db->num_collections = 0;
    db->initialized = 1;
    strcpy(db->db_path, ".");
}

/* === Collection DDL (L3: Engineering — schema management) === */

/* L3: Collection creation — analogous to CREATE TABLE.
 * Allocates storage for vectors and IDs, initializes the selected index type.
 * Returns collection index in the database array. */
int vdb_create_collection(VectorDB *db, const VDBConfig *config) {
    if (!db || !config || db->num_collections >= VDB_MAX_COLLECTIONS) return -1;
    if (config->dimension <= 0 || config->dimension > DIM_MAX) return -1;
    if (config->max_vectors <= 0) return -1;

    /* Check duplicate name */
    for (int i = 0; i < db->num_collections; i++) {
        if (strcmp(db->collections[i].config.name, config->name) == 0)
            return -1;
    }

    int idx = db->num_collections;
    VDBCollection *col = &db->collections[idx];

    memcpy(&col->config, config, sizeof(VDBConfig));
    col->capacity = config->max_vectors;
    col->num_vectors = 0;
    col->index_built = 0;
    col->index_trained = 0;
    col->insert_count = 0;
    col->search_count = 0;
    col->total_search_time_ms = 0.0;

    /* Allocate vector storage */
    col->vectors = (Vector *)malloc(col->capacity * sizeof(Vector));
    col->ids     = (int *)malloc(col->capacity * sizeof(int));
    if (!col->vectors || !col->ids) {
        free(col->vectors); free(col->ids);
        return -1;
    }

    /* Initialize selected index type */
    switch (config->index_type) {
    case VDB_INDEX_HNSW:
        hnsw_init(&col->hnsw, config->hnsw_M, config->hnsw_ef_construction);
        break;
    case VDB_INDEX_IVF:
        ivf_init(&col->ivf);
        break;
    case VDB_INDEX_LSH:
        lsh_init(&col->lsh);
        break;
    case VDB_INDEX_FLAT:
        break;
    }

    db->num_collections++;
    return idx;
}

/* L2: Drop collection — free all resources.
 * Analogous to DROP TABLE. All data is lost. */
int vdb_drop_collection(VectorDB *db, const char *name) {
    if (!db || !name) return -1;
    for (int i = 0; i < db->num_collections; i++) {
        if (strcmp(db->collections[i].config.name, name) == 0) {
            free(db->collections[i].vectors);
            free(db->collections[i].ids);
            /* Shift remaining collections down */
            for (int j = i; j < db->num_collections - 1; j++) {
                db->collections[j] = db->collections[j + 1];
            }
            db->num_collections--;
            return 0;
        }
    }
    return -1;
}

VDBCollection *vdb_get_collection(VectorDB *db, const char *name) {
    if (!db || !name) return NULL;
    for (int i = 0; i < db->num_collections; i++) {
        if (strcmp(db->collections[i].config.name, name) == 0)
            return &db->collections[i];
    }
    return NULL;
}

VDBCollection *vdb_get_collection_by_idx(VectorDB *db, int idx) {
    if (!db || idx < 0 || idx >= db->num_collections) return NULL;
    return &db->collections[idx];
}

/* === Data Operations (L7: Application) === */

/* L2: Insert with duplicate ID check.
 * If ID already exists, update the vector in-place.
 * This implements UPSERT semantics (common in vector DBs). */
int vdb_insert(VDBCollection *col, const Vector *vec, int id) {
    if (!col || !vec) return -1;

    /* Check for existing ID — update in-place */
    for (int i = 0; i < col->num_vectors; i++) {
        if (col->ids[i] == id) {
            col->vectors[i] = *vec;
            col->index_built = 0;
            col->insert_count++;
            return 0;
        }
    }

    if (col->num_vectors >= col->capacity) return -1;
    int n = col->num_vectors;
    col->vectors[n] = *vec;
    col->ids[n] = id;
    col->num_vectors++;
    col->insert_count++;
    col->index_built = 0;
    return 0;
}

/* L5: Batch insert — optimized bulk loading.
 * Faster than N individual inserts by avoiding repeated bounds checks.
 * Also defers index updates until vdb_build_index is called. */
int vdb_insert_batch(VDBCollection *col, const Vector *vecs,
                     const int *ids, int n) {
    if (!col || !vecs || !ids || n <= 0) return 0;
    int inserted = 0;
    for (int i = 0; i < n; i++) {
        if (col->num_vectors >= col->capacity) break;
        col->vectors[col->num_vectors] = vecs[i];
        col->ids[col->num_vectors] = ids[i];
        col->num_vectors++;
        inserted++;
    }
    col->insert_count += inserted;
    col->index_built = 0;
    return inserted;
}

/* L7: Search dispatcher — routes to the correct index implementation.
 * This is the primary application-facing API for vector search. */
int vdb_search(const VDBCollection *col, const Vector *query,
               int k, KNNResult *result) {
    if (!col || !query || !result || k <= 0) return -1;
    if (col->num_vectors == 0) { knn_result_init(result, 0); return 0; }

    clock_t t0 = clock();

    switch (col->config.index_type) {
    case VDB_INDEX_FLAT:
        knn_brute_force(col->vectors, col->num_vectors, query, k, result);
        break;
    case VDB_INDEX_HNSW:
        if (!col->index_built) vdb_build_index((VDBCollection *)col);
        hnsw_search(&col->hnsw, query, k, col->config.hnsw_ef_search, result);
        break;
    case VDB_INDEX_IVF:
        if (!col->index_trained) vdb_train_index((VDBCollection *)col);
        ivf_search(&col->ivf, query, k, col->config.ivf_nprobe, result);
        break;
    case VDB_INDEX_LSH:
        lsh_search(&col->lsh, query, k, 1, result);
        break;
    }

    clock_t t1 = clock();
    ((VDBCollection *)col)->search_count++;
    ((VDBCollection *)col)->total_search_time_ms +=
        (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;

    return 0;
}

/* L2: Delete vector by ID.
 * Uses swap-with-last removal (O(1)). Invalidates index ordering. */
int vdb_delete(VDBCollection *col, int id) {
    if (!col) return -1;
    for (int i = 0; i < col->num_vectors; i++) {
        if (col->ids[i] == id) {
            int last = col->num_vectors - 1;
            if (i != last) {
                col->vectors[i] = col->vectors[last];
                col->ids[i] = col->ids[last];
            }
            col->num_vectors--;
            col->index_built = 0;
            return 0;
        }
    }
    return -1;
}

/* L2: Get vector by ID — linear scan (O(n)).
 * For large collections, a hash map would be preferred (L8: optimization). */
int vdb_get_vector(const VDBCollection *col, int id, Vector *out) {
    if (!col || !out) return 0;
    for (int i = 0; i < col->num_vectors; i++) {
        if (col->ids[i] == id) { *out = col->vectors[i]; return 1; }
    }
    return 0;
}

/* === Index Operations (L3: Engineering) === */

/* L3: Index building — inserts all vectors into the selected index.
 * For HNSW, this is the primary construction step.
 * For IVF, this is called after training to populate inverted lists.
 * For LSH, this inserts all vectors into hash tables.
 * For Flat, no index is built (search scans all vectors). */
int vdb_build_index(VDBCollection *col) {
    if (!col || col->num_vectors == 0) return -1;

    switch (col->config.index_type) {
    case VDB_INDEX_FLAT:
        break;
    case VDB_INDEX_HNSW:
        hnsw_init(&col->hnsw, col->config.hnsw_M,
                  col->config.hnsw_ef_construction);
        for (int i = 0; i < col->num_vectors; i++) {
            hnsw_insert(&col->hnsw, &col->vectors[i], col->ids[i]);
        }
        break;
    case VDB_INDEX_IVF:
        if (!col->index_trained) vdb_train_index(col);
        for (int i = 0; i < col->num_vectors; i++) {
            ivf_add(&col->ivf, &col->vectors[i], col->ids[i]);
        }
        break;
    case VDB_INDEX_LSH:
        lsh_init(&col->lsh);
        for (int i = 0; i < col->num_vectors; i++) {
            lsh_insert(&col->lsh, &col->vectors[i], col->ids[i]);
        }
        break;
    }
    col->index_built = 1;
    return 0;
}

/* L5: Index training — k-means clustering for IVF.
 * Only meaningful for IVF index type. Other types are no-ops.
 * Training is the most expensive operation: O(n·nlist·dim·iters). */
int vdb_train_index(VDBCollection *col) {
    if (!col || col->num_vectors == 0) return -1;
    if (col->config.index_type != VDB_INDEX_IVF) {
        col->index_trained = 1;
        return 0;
    }
    ivf_init(&col->ivf);
    ivf_train(&col->ivf, col->vectors, col->num_vectors,
              col->config.ivf_nlist);
    col->index_trained = 1;
    return 0;
}

/* === Persistence (L6) === */

/* L6: Save entire database.
 * Writes all collections with their vectors and indices to a binary file.
 * Format: [header][collection 0]...[collection N-1][trailer] */
int vdb_save(VectorDB *db, const char *filepath) {
    if (!db || !filepath) return -1;
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -1;

    ser_write_u32(fp, VDB_FILE_MAGIC);
    ser_write_u32(fp, VDB_FILE_VERSION);
    ser_write_u32(fp, db->num_collections);

    for (int i = 0; i < db->num_collections; i++) {
        VDBCollection *col = &db->collections[i];

        /* Write config */
        ser_write_string(fp, col->config.name);
        ser_write_i32(fp, col->config.dimension);
        ser_write_i32(fp, col->config.index_type);
        ser_write_i32(fp, col->config.metric);
        ser_write_i32(fp, col->config.hnsw_M);
        ser_write_i32(fp, col->config.hnsw_ef_construction);
        ser_write_i32(fp, col->config.hnsw_ef_search);
        ser_write_i32(fp, col->config.ivf_nlist);
        ser_write_i32(fp, col->config.ivf_nprobe);
        ser_write_i32(fp, col->config.max_vectors);

        /* Write vector data */
        ser_write_i32(fp, col->num_vectors);
        for (int j = 0; j < col->num_vectors; j++) {
            ser_write_vector(fp, &col->vectors[j]);
            ser_write_i32(fp, col->ids[j]);
        }

        /* Write index if built */
        ser_write_i32(fp, col->index_built);
        ser_write_i32(fp, col->index_trained);
        if (col->index_built) {
            switch (col->config.index_type) {
            case VDB_INDEX_HNSW: ser_write_hnsw(fp, &col->hnsw); break;
            case VDB_INDEX_IVF:  ser_write_ivf(fp, &col->ivf); break;
            case VDB_INDEX_LSH:  ser_write_lsh(fp, &col->lsh); break;
            default: break;
            }
        }

        /* Write stats */
        ser_write_i32(fp, 0);
        ser_write_i32(fp, 0);
    }

    fclose(fp);
    return 0;
}

/* L6: Load database from file.
 * Reconstructs all collections and indices from binary format. */
int vdb_load(VectorDB *db, const char *filepath) {
    if (!db || !filepath) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    unsigned int magic, version, num_cols;
    if (!ser_read_u32(fp, &magic) || magic != VDB_FILE_MAGIC) { fclose(fp); return -1; }
    if (!ser_read_u32(fp, &version) || version != VDB_FILE_VERSION) { fclose(fp); return -1; }
    if (!ser_read_u32(fp, &num_cols)) { fclose(fp); return -1; }

    vdb_init(db);
    for (unsigned int i = 0; i < num_cols; i++) {
        VDBConfig cfg;
        ser_read_string(fp, cfg.name, VDB_MAX_NAME_LEN);
        int dim, idx_type, metric, hnsw_m, hnsw_efc, hnsw_efs, ivf_nl, ivf_np, maxv;
        ser_read_i32(fp, &dim);       cfg.dimension = dim;
        ser_read_i32(fp, &idx_type);  cfg.index_type = (VDBIndexType)idx_type;
        ser_read_i32(fp, &metric);    cfg.metric = metric;
        ser_read_i32(fp, &hnsw_m);    cfg.hnsw_M = hnsw_m;
        ser_read_i32(fp, &hnsw_efc);  cfg.hnsw_ef_construction = hnsw_efc;
        ser_read_i32(fp, &hnsw_efs);  cfg.hnsw_ef_search = hnsw_efs;
        ser_read_i32(fp, &ivf_nl);    cfg.ivf_nlist = ivf_nl;
        ser_read_i32(fp, &ivf_np);    cfg.ivf_nprobe = ivf_np;
        ser_read_i32(fp, &maxv);      cfg.max_vectors = maxv;

        int col_idx = vdb_create_collection(db, &cfg);
        if (col_idx < 0) { fclose(fp); return -1; }
        VDBCollection *col = &db->collections[col_idx];

        int num_vecs;
        ser_read_i32(fp, &num_vecs);
        for (int j = 0; j < num_vecs && j < col->capacity; j++) {
            Vector v; int id;
            ser_read_vector(fp, &v);
            ser_read_i32(fp, &id);
            col->vectors[col->num_vectors] = v;
            col->ids[col->num_vectors] = id;
            col->num_vectors++;
        }

        int idx_built, idx_trained;
        ser_read_i32(fp, &idx_built);
        ser_read_i32(fp, &idx_trained);
        if (idx_built) {
            switch (cfg.index_type) {
            case VDB_INDEX_HNSW: ser_read_hnsw(fp, &col->hnsw); break;
            case VDB_INDEX_IVF:  ser_read_ivf(fp, &col->ivf); break;
            case VDB_INDEX_LSH:  ser_read_lsh(fp, &col->lsh); break;
            default: break;
            }
            col->index_built = 1;
        }
        col->index_trained = idx_trained;

        /* Skip stats */
        int dummy;
        ser_read_i32(fp, &dummy);
        ser_read_i32(fp, &dummy);
    }

    fclose(fp);
    return 0;
}

int vdb_collection_save(const VDBCollection *col, const char *filepath) {
    if (!col || !filepath) return -1;
    VectorDB tmp_db;
    vdb_init(&tmp_db);
    tmp_db.collections[0] = *col;
    tmp_db.num_collections = 1;
    return vdb_save(&tmp_db, filepath);
}

int vdb_collection_load(VDBCollection *col, const char *filepath) {
    if (!col || !filepath) return -1;
    VectorDB tmp_db;
    if (vdb_load(&tmp_db, filepath) != 0) return -1;
    if (tmp_db.num_collections < 1) return -1;
    *col = tmp_db.collections[0];
    tmp_db.collections[0].vectors = NULL;
    tmp_db.collections[0].ids = NULL;
    return 0;
}

/* === Diagnostics (L7: Application — monitoring) === */

void vdb_print_collection_stats(const VDBCollection *col) {
    if (!col) return;
    printf("=== Collection: %s ===\n", col->config.name);
    printf("  Dimension:        %d\n", col->config.dimension);
    printf("  Index type:       %d\n", col->config.index_type);
    printf("  Vectors:          %d / %d\n", col->num_vectors, col->capacity);
    printf("  Index built:      %s\n", col->index_built ? "yes" : "no");
    printf("  Index trained:    %s\n", col->index_trained ? "yes" : "no");
    printf("  Total inserts:    %lld\n", col->insert_count);
    printf("  Total searches:   %lld\n", col->search_count);
    if (col->search_count > 0) {
        printf("  Avg search time:  %.4f ms\n",
               col->total_search_time_ms / col->search_count);
    }
    printf("  Memory (vectors): %zu bytes\n",
           col->capacity * sizeof(Vector));
    printf("==========================\n");
}

void vdb_print_stats(const VectorDB *db) {
    if (!db) return;
    printf("===== VectorDB Stats =====\n");
    printf("  Collections: %d\n", db->num_collections);
    printf("  Path:        %s\n", db->db_path);
    for (int i = 0; i < db->num_collections; i++) {
        vdb_print_collection_stats(&db->collections[i]);
    }
    printf("==========================\n");
}