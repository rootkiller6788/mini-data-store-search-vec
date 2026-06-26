#ifndef IVF_PQ_H
#define IVF_PQ_H

#include "vector_math.h"
#include "exact_knn.h"

#define IVF_MAX_CENTROIDS  256
#define IVF_MAX_VECTORS    50000
#define IVF_NPROBE         10
#define PQ_M               8
#define PQ_KS              256
#define PQ_SUBDIM          (DIM_MAX / PQ_M)

typedef struct {
    float centers[IVF_MAX_CENTROIDS][DIM_MAX];
    int   n_centroids;
    int   n_iters;
} KMeans;

typedef struct {
    int codes[PQ_M];
} PQCode;

typedef struct {
    float   subquantizers[PQ_M][PQ_KS][PQ_SUBDIM];
    int     n_subquantizers;
} PQCodebook;

typedef struct {
    int list_ids[IVF_MAX_VECTORS];
    int list_size;
} InvertedList;

typedef struct {
    KMeans       kmeans;
    PQCodebook   codebook;
    InvertedList lists[IVF_MAX_CENTROIDS];
    PQCode       codes[IVF_MAX_VECTORS];
    Vector       vectors[IVF_MAX_VECTORS];
    int          id_map[IVF_MAX_VECTORS];
    int          num_vectors;
    int          trained;
} IVFIndex;

void ivf_init(IVFIndex *index);
void ivf_train(IVFIndex *index, const Vector *vectors, int n, int nlist);
void ivf_add(IVFIndex *index, const Vector *vec, int id);
void ivf_search(const IVFIndex *index, const Vector *query,
                int k, int nprobe, KNNResult *result);
void ivf_print_stats(const IVFIndex *index);

#endif
