#ifndef EXACT_KNN_H
#define EXACT_KNN_H

#include "vector_math.h"

#define KNN_MAX_K 256

typedef struct {
    int   id;
    float distance;
} Neighbor;

typedef struct {
    Neighbor neighbors[KNN_MAX_K];
    int      k;
    int      count;
} KNNResult;

void knn_brute_force(const Vector *dataset, int n,
                     const Vector *query, int k,
                     KNNResult *result);

KNNResult knn_search(const Vector *dataset, int n,
                     const Vector *query, int k);

void knn_print_result(const KNNResult *result);

#endif
