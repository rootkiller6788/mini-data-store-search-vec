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

float knn_recall_at_k(const KNNResult *ground_truth,
                      const KNNResult *approx,
                      int k);

int knn_intersect_count(const KNNResult *a, const KNNResult *b);

void knn_result_init(KNNResult *r, int k);

void knn_result_sort(KNNResult *r);

void knn_result_add(KNNResult *r, int id, float dist);

void knn_result_prune(KNNResult *r, int k);

float knn_result_max_dist(const KNNResult *r);

int knn_result_has_id(const KNNResult *r, int id);

#endif
