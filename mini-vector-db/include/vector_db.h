#ifndef VECTOR_DB_H
#define VECTOR_DB_H

#include "vector_math.h"
#include "exact_knn.h"
#include "hnsw.h"
#include "ivf_pq.h"
#include "lsh.h"

#define VDB_MAX_COLLECTIONS  16
#define VDB_MAX_NAME_LEN     64
#define VDB_FILE_MAGIC       0x56444354
#define VDB_FILE_VERSION     1

typedef enum {
    VDB_INDEX_FLAT = 0,
    VDB_INDEX_HNSW = 1,
    VDB_INDEX_IVF  = 2,
    VDB_INDEX_LSH  = 3
} VDBIndexType;

typedef struct {
    char        name[VDB_MAX_NAME_LEN];
    int         dimension;
    VDBIndexType index_type;
    int         metric;
    int hnsw_M;
    int hnsw_ef_construction;
    int hnsw_ef_search;
    int ivf_nlist;
    int ivf_nprobe;
    int max_vectors;
} VDBConfig;

typedef struct {
    VDBConfig  config;
    Vector    *vectors;
    int       *ids;
    int        num_vectors;
    int        capacity;
    HNSWGraph  hnsw;
    IVFIndex   ivf;
    LSHTable   lsh;
    int        index_built;
    int        index_trained;
    long long  insert_count;
    long long  search_count;
    double     total_search_time_ms;
} VDBCollection;

typedef struct {
    VDBCollection collections[VDB_MAX_COLLECTIONS];
    int           num_collections;
    char          db_path[256];
    int           initialized;
} VectorDB;

void vdb_init(VectorDB *db);
int vdb_create_collection(VectorDB *db, const VDBConfig *config);
int vdb_drop_collection(VectorDB *db, const char *name);
VDBCollection *vdb_get_collection(VectorDB *db, const char *name);
VDBCollection *vdb_get_collection_by_idx(VectorDB *db, int idx);
int vdb_insert(VDBCollection *col, const Vector *vec, int id);
int vdb_insert_batch(VDBCollection *col, const Vector *vecs, const int *ids, int n);
int vdb_search(const VDBCollection *col, const Vector *query, int k, KNNResult *result);
int vdb_delete(VDBCollection *col, int id);
int vdb_get_vector(const VDBCollection *col, int id, Vector *out);
int vdb_build_index(VDBCollection *col);
int vdb_train_index(VDBCollection *col);
int vdb_save(VectorDB *db, const char *filepath);
int vdb_load(VectorDB *db, const char *filepath);
int vdb_collection_save(const VDBCollection *col, const char *filepath);
int vdb_collection_load(VDBCollection *col, const char *filepath);
void vdb_print_collection_stats(const VDBCollection *col);
void vdb_print_stats(const VectorDB *db);

#endif