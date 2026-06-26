#include "hnsw.h"
#include "ivf_pq.h"
#include "lsh.h"
#include "exact_knn.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define N_VECTORS   1000
#define N_QUERIES   50
#define DIM         128
#define K           10

extern float knn_recall_at_k(const KNNResult *ground_truth,
                             const KNNResult *approx, int k);

static void generate_data(Vector *dataset, Vector *queries,
                          int n, int nq, int dim)
{
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < dim; d++) {
            dataset[i].data[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        }
        dataset[i].dim = dim;
    }
    for (int q = 0; q < nq; q++) {
        for (int d = 0; d < dim; d++) {
            queries[q].data[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        }
        queries[q].dim = dim;
    }
}

typedef struct {
    const char *name;
    double      avg_time_ms;
    double      avg_recall;
} BenchResult;

int main(void)
{
    srand((unsigned)time(NULL));

    Vector *dataset = (Vector *)malloc(N_VECTORS * sizeof(Vector));
    Vector *queries = (Vector *)malloc(N_QUERIES * sizeof(Vector));

    printf("=== ANN Algorithm Comparison ===\n");
    printf("Dataset: %d vectors, %d dim, %d queries, K=%d\n\n",
           N_VECTORS, DIM, N_QUERIES, K);

    generate_data(dataset, queries, N_VECTORS, N_QUERIES, DIM);

    printf("--- Step 1: Brute-Force Ground Truth ---\n");
    KNNResult *ground_truth = (KNNResult *)malloc(N_QUERIES * sizeof(KNNResult));
    clock_t gt_start = clock();
    for (int q = 0; q < N_QUERIES; q++) {
        ground_truth[q] = knn_search(dataset, N_VECTORS, &queries[q], K);
    }
    clock_t gt_end = clock();
    printf("  Ground truth computed in %.3f ms\n",
           (double)(gt_end - gt_start) / CLOCKS_PER_SEC * 1000.0);

    printf("\n--- Step 2: Build HNSW Index ---\n");
    HNSWGraph hnsw;
    hnsw_init(&hnsw, HNSW_M, HNSW_EF_CONSTRUCT);
    clock_t hb_start = clock();
    for (int i = 0; i < N_VECTORS; i++) {
        hnsw_insert(&hnsw, &dataset[i], i);
    }
    clock_t hb_end = clock();
    hnsw_print_stats(&hnsw);

    printf("\n--- Step 3: Train & Build IVF-PQ Index ---\n");
    IVFIndex ivf;
    ivf_init(&ivf);
    clock_t ib_start = clock();
    ivf_train(&ivf, dataset, N_VECTORS, IVF_MAX_CENTROIDS);
    for (int i = 0; i < N_VECTORS; i++) {
        ivf_add(&ivf, &dataset[i], i);
    }
    clock_t ib_end = clock();
    ivf_print_stats(&ivf);

    printf("\n--- Step 4: Build LSH Index ---\n");
    LSHTable lsh;
    lsh_init(&lsh);
    clock_t lb_start = clock();
    for (int i = 0; i < N_VECTORS; i++) {
        lsh_insert(&lsh, &dataset[i], i);
    }
    clock_t lb_end = clock();
    lsh_print_stats(&lsh);

    printf("\n--- Step 5: Query & Compare ---\n\n");

    BenchResult results[4];

    double total_brute = 0.0;
    for (int q = 0; q < N_QUERIES; q++) {
        clock_t s = clock();
        KNNResult r = knn_search(dataset, N_VECTORS, &queries[q], K);
        clock_t e = clock();
        total_brute += (double)(e - s);
    }
    results[0].name = "Brute-Force";
    results[0].avg_time_ms = total_brute / N_QUERIES / CLOCKS_PER_SEC * 1000.0;
    results[0].avg_recall = 1.000;

    double total_hnsw_time = 0.0, total_hnsw_recall = 0.0;
    for (int q = 0; q < N_QUERIES; q++) {
        clock_t s = clock();
        KNNResult r;
        hnsw_search(&hnsw, &queries[q], K, HNSW_EF_DEFAULT, &r);
        clock_t e = clock();
        total_hnsw_time += (double)(e - s);
        total_hnsw_recall += knn_recall_at_k(&ground_truth[q], &r, K);
    }
    results[1].name = "HNSW";
    results[1].avg_time_ms = total_hnsw_time / N_QUERIES / CLOCKS_PER_SEC * 1000.0;
    results[1].avg_recall = total_hnsw_recall / N_QUERIES;

    double total_ivf_time = 0.0, total_ivf_recall = 0.0;
    for (int q = 0; q < N_QUERIES; q++) {
        clock_t s = clock();
        KNNResult r;
        ivf_search(&ivf, &queries[q], K, IVF_NPROBE, &r);
        clock_t e = clock();
        total_ivf_time += (double)(e - s);
        total_ivf_recall += knn_recall_at_k(&ground_truth[q], &r, K);
    }
    results[2].name = "IVF-PQ";
    results[2].avg_time_ms = total_ivf_time / N_QUERIES / CLOCKS_PER_SEC * 1000.0;
    results[2].avg_recall = total_ivf_recall / N_QUERIES;

    double total_lsh_time = 0.0, total_lsh_recall = 0.0;
    for (int q = 0; q < N_QUERIES; q++) {
        clock_t s = clock();
        KNNResult r;
        lsh_search(&lsh, &queries[q], K, 1, &r);
        clock_t e = clock();
        total_lsh_time += (double)(e - s);
        total_lsh_recall += knn_recall_at_k(&ground_truth[q], &r, K);
    }
    results[3].name = "LSH";
    results[3].avg_time_ms = total_lsh_time / N_QUERIES / CLOCKS_PER_SEC * 1000.0;
    results[3].avg_recall = total_lsh_recall / N_QUERIES;

    printf("  %-15s %12s %12s %12s\n", "Algorithm", "Time(ms)", "Recall@K", "QPS");
    printf("  %-15s %12s %12s %12s\n", "---------", "--------", "--------", "---");
    for (int i = 0; i < 4; i++) {
        double qps = results[i].avg_time_ms > 0.001
            ? 1000.0 / results[i].avg_time_ms : 0.0;
        printf("  %-15s %12.4f %12.4f %12.0f\n",
               results[i].name,
               results[i].avg_time_ms,
               results[i].avg_recall,
               qps);
    }

    printf("\n  Build times:\n");
    printf("    HNSW:    %.3f ms\n",
           (double)(hb_end - hb_start) / CLOCKS_PER_SEC * 1000.0);
    printf("    IVF-PQ:  %.3f ms\n",
           (double)(ib_end - ib_start) / CLOCKS_PER_SEC * 1000.0);
    printf("    LSH:     %.3f ms\n",
           (double)(lb_end - lb_start) / CLOCKS_PER_SEC * 1000.0);

    free(dataset);
    free(queries);
    free(ground_truth);
    return 0;
}
