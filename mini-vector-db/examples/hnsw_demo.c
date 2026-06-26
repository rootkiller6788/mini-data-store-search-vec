#include "hnsw.h"
#include "exact_knn.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define N_VECTORS   500
#define N_QUERIES   20
#define DIM         128
#define K           5

int main(void)
{
    srand((unsigned)time(NULL));

    Vector dataset[N_VECTORS];
    Vector queries[N_QUERIES];

    printf("Generating %d vectors (dim=%d)...\n", N_VECTORS, DIM);
    for (int i = 0; i < N_VECTORS; i++) {
        for (int d = 0; d < DIM; d++) {
            dataset[i].data[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        }
        dataset[i].dim = DIM;
    }

    for (int q = 0; q < N_QUERIES; q++) {
        for (int d = 0; d < DIM; d++) {
            queries[q].data[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        }
        queries[q].dim = DIM;
    }

    printf("\n--- Building HNSW Index ---\n");
    HNSWGraph graph;
    hnsw_init(&graph, HNSW_M, HNSW_EF_CONSTRUCT);

    for (int i = 0; i < N_VECTORS; i++) {
        hnsw_insert(&graph, &dataset[i], i);
    }
    hnsw_print_stats(&graph);

    printf("\n--- Comparing HNSW vs Brute-Force (K=%d, %d queries) ---\n\n", K, N_QUERIES);

    float total_recall = 0.0f;
    clock_t total_hnsw = 0;
    clock_t total_bf = 0;

    for (int q = 0; q < N_QUERIES; q++) {
        KNNResult bf_result = knn_search(dataset, N_VECTORS, &queries[q], K);

        clock_t hs = clock();
        KNNResult hnsw_result;
        hnsw_search(&graph, &queries[q], K, HNSW_EF_DEFAULT, &hnsw_result);
        clock_t he = clock();

        clock_t bs = clock();
        KNNResult bf_result2 = knn_search(dataset, N_VECTORS, &queries[q], K);
        clock_t be = clock();

        float recall = knn_recall_at_k(&bf_result, &hnsw_result, K);
        total_recall += recall;
        total_hnsw += (he - hs);
        total_bf   += (be - bs);

        printf("  Query %2d: HNSW ids=[", q);
        for (int i = 0; i < hnsw_result.count; i++) {
            printf("%d%s", hnsw_result.neighbors[i].id,
                   i < hnsw_result.count - 1 ? "," : "");
        }
        printf("]  BF ids=[");
        for (int i = 0; i < bf_result.count; i++) {
            printf("%d%s", bf_result.neighbors[i].id,
                   i < bf_result.count - 1 ? "," : "");
        }
        printf("]  recall@%d=%.2f\n", K, recall);
    }

    printf("\n--- Summary ---\n");
    printf("  Avg Recall@%d:      %.4f\n", K, total_recall / N_QUERIES);
    printf("  Avg HNSW time:      %.3f ms\n",
           (double)total_hnsw / N_QUERIES / CLOCKS_PER_SEC * 1000.0);
    printf("  Avg BF time:        %.3f ms\n",
           (double)total_bf / N_QUERIES / CLOCKS_PER_SEC * 1000.0);
    printf("  Speedup:            %.2fx\n",
           (total_bf > 0) ? (double)total_bf / (double)total_hnsw : 0.0);

    return 0;
}
