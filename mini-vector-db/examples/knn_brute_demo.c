#include "exact_knn.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define N 100
#define DIM 128
#define K 5

int main(void)
{
    srand((unsigned)time(NULL));

    Vector dataset[N];
    Vector query;

    printf("Generating %d random %d-dim vectors...\n", N, DIM);
    for (int i = 0; i < N; i++) {
        for (int d = 0; d < DIM; d++) {
            dataset[i].data[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        }
        dataset[i].dim = DIM;
    }

    for (int d = 0; d < DIM; d++) {
        query.data[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    }
    query.dim = DIM;

    printf("\n--- Brute-Force KNN (K=%d) ---\n\n", K);

    KNNResult result = knn_search(dataset, N, &query, K);
    knn_print_result(&result);

    printf("\n");

    float query_norm = 0.0f;
    for (int d = 0; d < DIM; d++) query_norm += query.data[d] * query.data[d];
    printf("Query L2 norm: %.4f, dim: %d\n", sqrtf(query_norm), query.dim);

    return 0;
}
