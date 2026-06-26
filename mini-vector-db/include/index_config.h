#ifndef INDEX_CONFIG_H
#define INDEX_CONFIG_H

#include "vector_math.h"
#include "exact_knn.h"

/* L5: Index Configuration & Builder Pattern
 *
 * The Index Builder pattern decouples index construction from usage.
 * This follows the Builder design pattern (GoF) adapted for C.
 *
 * Key design decisions encoded as configuration parameters:
 * - Space/time tradeoff (precision vs speed)
 * - Memory budget (compression level)
 * - Construction parallelism
 */

#define IDX_CFG_MAX_NAME 64

typedef enum {
    IDX_STRATEGY_ACCURACY = 0,  /* maximize recall */
    IDX_STRATEGY_SPEED    = 1,  /* minimize latency */
    IDX_STRATEGY_BALANCED = 2,  /* balanced tradeoff */
    IDX_STRATEGY_MEMORY   = 3   /* minimize memory footprint */
} IndexStrategy;

/* L1: IndexConfig — all parameters needed to construct an index */
typedef struct {
    char          name[IDX_CFG_MAX_NAME];
    int           dimension;
    int           metric_type;
    IndexStrategy strategy;

    /* HNSW parameters */
    int hnsw_M;
    int hnsw_ef_construction;
    int hnsw_ef_search;
    int hnsw_max_elements;

    /* IVF-PQ parameters */
    int ivf_nlist;
    int ivf_nprobe;
    int pq_M;
    int pq_nbits;

    /* LSH parameters */
    int lsh_num_tables;
    int lsh_num_hashes;

    /* General */
    int max_vectors;
    int random_seed;
} IndexConfig;

/* Initialize config with sensible defaults for given dimension */
void index_config_init(IndexConfig *cfg, int dimension);

/* Set strategy (adjusts internal parameters accordingly) */
void index_config_set_strategy(IndexConfig *cfg, IndexStrategy strategy);

/* Auto-tune parameters based on expected data size */
void index_config_autotune(IndexConfig *cfg, int expected_n);

/* Validate configuration — returns 0 if valid, -1 on error with message */
int index_config_validate(const IndexConfig *cfg, char *err_msg, int err_size);

/* Print human-readable configuration */
void index_config_print(const IndexConfig *cfg);

/* Estimate memory usage (bytes) for the configured index with n vectors */
size_t index_config_estimate_memory(const IndexConfig *cfg, int n);

/* Compare two configs — returns 1 if equal, 0 otherwise */
int index_config_equals(const IndexConfig *a, const IndexConfig *b);

#endif