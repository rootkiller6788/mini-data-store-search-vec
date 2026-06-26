#include "index_config.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* L3: Index Configuration Builder
 *
 * Provides sensible defaults and strategy-based tuning for
 * ANN index parameters. This encapsulates the domain knowledge
 * of parameter selection (the "art" of ANN tuning).
 *
 * Parameter selection rules derived from:
 * - HNSW paper (Malkov & Yashunin, 2018): M ∈ [5, 48], efConstruction ≥ M
 * - FAISS wiki: nlist ≈ sqrt(N) to 4×sqrt(N)
 * - LSH theory: L ≈ log(1/δ), K ≈ 1/ε
 */

void index_config_init(IndexConfig *cfg, int dimension) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->dimension = dimension;
    cfg->strategy = IDX_STRATEGY_BALANCED;
    cfg->metric_type = 0;

    /* HNSW defaults — conservative */
    cfg->hnsw_M = 16;
    cfg->hnsw_ef_construction = 200;
    cfg->hnsw_ef_search = 64;
    cfg->hnsw_max_elements = 100000;

    /* IVF-PQ defaults */
    cfg->ivf_nlist = 256;
    cfg->ivf_nprobe = 10;
    cfg->pq_M = 8;
    cfg->pq_nbits = 8;

    /* LSH defaults */
    cfg->lsh_num_tables = 16;
    cfg->lsh_num_hashes = 8;

    cfg->max_vectors = 100000;
    cfg->random_seed = 42;
}

/* L5: Strategy-based parameter tuning.
 * Adjusts parameters to optimize for different tradeoffs:
 * - ACCURACY: more neighbors (M), more search width (ef)
 * - SPEED: fewer probes, smaller search width
 * - MEMORY: smaller M, fewer hash tables */
void index_config_set_strategy(IndexConfig *cfg, IndexStrategy strategy) {
    cfg->strategy = strategy;
    switch (strategy) {
    case IDX_STRATEGY_ACCURACY:
        cfg->hnsw_M = 32;
        cfg->hnsw_ef_construction = 500;
        cfg->hnsw_ef_search = 256;
        cfg->ivf_nprobe = 32;
        cfg->pq_M = 16;
        cfg->lsh_num_tables = 32;
        break;
    case IDX_STRATEGY_SPEED:
        cfg->hnsw_M = 8;
        cfg->hnsw_ef_construction = 50;
        cfg->hnsw_ef_search = 16;
        cfg->ivf_nprobe = 2;
        cfg->pq_M = 4;
        cfg->lsh_num_tables = 4;
        break;
    case IDX_STRATEGY_MEMORY:
        cfg->hnsw_M = 4;
        cfg->hnsw_ef_construction = 100;
        cfg->hnsw_ef_search = 32;
        cfg->ivf_nprobe = 4;
        cfg->pq_M = 4;
        cfg->lsh_num_tables = 4;
        break;
    case IDX_STRATEGY_BALANCED:
    default:
        break;
    }
}

/* L5: Auto-tuning based on expected dataset size.
 * nlist ≈ 4 × sqrt(n)  — FAISS heuristic
 * For HNSW: M scales with log(n) to maintain log-time search. */
void index_config_autotune(IndexConfig *cfg, int expected_n) {
    if (expected_n <= 0) return;

    /* nlist heuristic: sqrt rule */
    int nlist = (int)(4.0 * sqrt((double)expected_n));
    if (nlist < 8) nlist = 8;
    if (nlist > 4096) nlist = 4096;
    cfg->ivf_nlist = nlist;

    /* HNSW M: scale with log(n) */
    int M = (int)(log2((double)expected_n) * 2.0);
    if (M < 4) M = 4;
    if (M > 64) M = 64;
    cfg->hnsw_M = M;

    /* ef_construction should be ≥ M * 2 */
    cfg->hnsw_ef_construction = cfg->hnsw_M * 8;

    /* ef_search default */
    cfg->hnsw_ef_search = cfg->hnsw_M * 4;

    /* nprobe: ~5% of nlist, bounded */
    cfg->ivf_nprobe = nlist / 20;
    if (cfg->ivf_nprobe < 1) cfg->ivf_nprobe = 1;
    if (cfg->ivf_nprobe > 64) cfg->ivf_nprobe = 64;

    /* PQ: more subquantizers for larger datasets */
    cfg->pq_M = (int)(log2((double)expected_n) / 2.0);
    if (cfg->pq_M < 4) cfg->pq_M = 4;
    if (cfg->pq_M > 16) cfg->pq_M = 16;

    cfg->max_vectors = expected_n * 2;
}

/* L3: Configuration validation.
 * Checks for parameter consistency and bounds violations.
 * Returns descriptive error message for invalid configs. */
int index_config_validate(const IndexConfig *cfg, char *err_msg, int err_size) {
    if (!cfg) { snprintf(err_msg, err_size, "NULL config"); return -1; }
    if (cfg->dimension <= 0 || cfg->dimension > DIM_MAX) {
        snprintf(err_msg, err_size, "Invalid dimension: %d", cfg->dimension);
        return -1;
    }
    if (cfg->hnsw_M < 2 || cfg->hnsw_M > 128) {
        snprintf(err_msg, err_size, "HNSW M out of range [2,128]: %d", cfg->hnsw_M);
        return -1;
    }
    if (cfg->hnsw_ef_construction < cfg->hnsw_M) {
        snprintf(err_msg, err_size, "ef_construction < M: %d < %d",
                 cfg->hnsw_ef_construction, cfg->hnsw_M);
        return -1;
    }
    if (cfg->ivf_nlist < 1 || cfg->ivf_nlist > 65536) {
        snprintf(err_msg, err_size, "ivf_nlist out of range: %d", cfg->ivf_nlist);
        return -1;
    }
    if (cfg->ivf_nprobe > cfg->ivf_nlist) {
        snprintf(err_msg, err_size, "nprobe > nlist: %d > %d",
                 cfg->ivf_nprobe, cfg->ivf_nlist);
        return -1;
    }
    if (cfg->max_vectors < 1) {
        snprintf(err_msg, err_size, "max_vectors must be positive");
        return -1;
    }
    return 0;
}

void index_config_print(const IndexConfig *cfg) {
    if (!cfg) return;
    printf("=== Index Configuration ===\n");
    printf("  Name:        %s\n", cfg->name);
    printf("  Dimension:   %d\n", cfg->dimension);
    printf("  Metric:      %d\n", cfg->metric_type);
    printf("  Strategy:    %d\n", cfg->strategy);
    printf("  --- HNSW ---\n");
    printf("    M:             %d\n", cfg->hnsw_M);
    printf("    ef_const:      %d\n", cfg->hnsw_ef_construction);
    printf("    ef_search:     %d\n", cfg->hnsw_ef_search);
    printf("  --- IVF-PQ ---\n");
    printf("    nlist:         %d\n", cfg->ivf_nlist);
    printf("    nprobe:        %d\n", cfg->ivf_nprobe);
    printf("    PQ_M:          %d\n", cfg->pq_M);
    printf("  --- LSH ---\n");
    printf("    num_tables:    %d\n", cfg->lsh_num_tables);
    printf("    num_hashes:    %d\n", cfg->lsh_num_hashes);
    printf("  --- General ---\n");
    printf("    max_vectors:   %d\n", cfg->max_vectors);
    printf("=========================\n");
}

/* L5: Memory estimation.
 * HNSW: ~(M + Mmax0) × max_level × 4 bytes per edge + vectors
 * IVF: ~nlist × dim × 4 + vectors × (dim × 4 + pq_M bytes)
 * LSH: ~tables × buckets × 4 bytes per entry */
size_t index_config_estimate_memory(const IndexConfig *cfg, int n) {
    size_t vector_bytes = (size_t)n * cfg->dimension * sizeof(float);
    size_t index_bytes = 0;

    /* HNSW estimate: avg_degree ≈ M at each level, avg_level ≈ log(n)/log(M) */
    int avg_level = (int)(log((double)n) / log((double)cfg->hnsw_M));
    if (avg_level < 1) avg_level = 1;
    index_bytes = (size_t)n * cfg->hnsw_M * avg_level * sizeof(int);

    /* IVF estimate */
    size_t ivf_bytes = (size_t)cfg->ivf_nlist * cfg->dimension * sizeof(float);
    ivf_bytes += (size_t)n * sizeof(int); /* list membership */
    ivf_bytes += (size_t)n * cfg->pq_M;   /* PQ codes */

    if (ivf_bytes > index_bytes) index_bytes = ivf_bytes;

    return vector_bytes + index_bytes;
}

int index_config_equals(const IndexConfig *a, const IndexConfig *b) {
    if (!a || !b) return 0;
    return (a->dimension            == b->dimension &&
            a->metric_type           == b->metric_type &&
            a->hnsw_M               == b->hnsw_M &&
            a->hnsw_ef_construction == b->hnsw_ef_construction &&
            a->hnsw_ef_search       == b->hnsw_ef_search &&
            a->ivf_nlist            == b->ivf_nlist &&
            a->ivf_nprobe           == b->ivf_nprobe &&
            a->pq_M                 == b->pq_M &&
            a->lsh_num_tables       == b->lsh_num_tables) ? 1 : 0;
}