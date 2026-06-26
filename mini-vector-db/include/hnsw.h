#ifndef HNSW_H
#define HNSW_H

#include "vector_math.h"
#include "exact_knn.h"

#define HNSW_MAX_NODES    10000
#define HNSW_M             16
#define HNSW_MMAX0         (2 * HNSW_M)
#define HNSW_EF_CONSTRUCT  200
#define HNSW_MAX_LEVEL     16
#define HNSW_EF_DEFAULT    64

typedef struct {
    int    id;
    int    level;
    int    neighbors[HNSW_MAX_LEVEL][HNSW_MMAX0];
    int    n_neighbors[HNSW_MAX_LEVEL];
    Vector vector;
} HNSWNode;

typedef struct {
    HNSWNode nodes[HNSW_MAX_NODES];
    int      entry_point;
    int      num_nodes;
    int      M;
    int      Mmax0;
    int      ef_construction;
} HNSWGraph;

void hnsw_init(HNSWGraph *graph, int M, int ef_construction);
void hnsw_insert(HNSWGraph *graph, const Vector *vec, int id);
void hnsw_search(const HNSWGraph *graph, const Vector *query,
                 int k, int ef, KNNResult *result);
void hnsw_print_stats(const HNSWGraph *graph);

#endif
