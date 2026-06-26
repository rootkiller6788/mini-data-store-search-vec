#ifndef LSH_H
#define LSH_H

#include "vector_math.h"
#include "exact_knn.h"

#define LSH_NUM_TABLES  32
#define LSH_NUM_HASHES  8
#define LSH_MAX_VECTORS 100000
#define LSH_HASH_BITS   10
#define LSH_TABLE_SIZE  (1 << LSH_HASH_BITS)

typedef struct {
    float random_proj[DIM_MAX];
    float bias;
} LSHHash;

typedef struct {
    int bucket_ids[LSH_MAX_VECTORS];
    int bucket_size;
} LSHBucket;

typedef struct {
    LSHHash     hashes[LSH_NUM_TABLES][LSH_NUM_HASHES];
    LSHBucket   buckets[LSH_NUM_TABLES][LSH_TABLE_SIZE];
    Vector      vectors[LSH_MAX_VECTORS];
    int         ids[LSH_MAX_VECTORS];
    int         num_vectors;
} LSHTable;

void lsh_init(LSHTable *table);
void lsh_insert(LSHTable *table, const Vector *vec, int id);
void lsh_search(const LSHTable *table, const Vector *query,
                int k, int num_probes, KNNResult *result);

#endif
