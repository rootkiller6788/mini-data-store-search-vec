#include "vector_math.h"
#include "exact_knn.h"
#include "hnsw.h"
#include "ivf_pq.h"
#include "lsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* L6: Index Evaluation & Benchmarking
 *
 * Standard IR metrics for evaluating ANN index quality:
 * - Recall@K: fraction of true top-K found by approximate method
 * - Precision@K: fraction of returned results that are in ground truth
 * - mAP (Mean Average Precision): area under precision-recall curve
 * - nDCG@K: discounted cumulative gain (position-weighted relevance)
 * - QPS (Queries Per Second): throughput measurement
 *
 * These follow the Cranfield evaluation paradigm from IR research
 * (Cleverdon, 1966) adapted to ANN search.
 */

/* L5: Recall@K — standard IR metric for ANN evaluation.
 * recall = |approx ∩ ground_truth| / |ground_truth|
 * Measures what fraction of true nearest neighbors were found. */
float eval_recall_at_k(const KNNResult *ground_truth,
                        const KNNResult *approx, int k) {
    if (!ground_truth || !approx) return 0.0f;
    int kk = ground_truth->count < k ? ground_truth->count : k;
    if (kk <= 0) return 0.0f;

    int hits = 0;
    for (int i = 0; i < approx->count && i < k; i++) {
        int aid = approx->neighbors[i].id;
        for (int j = 0; j < ground_truth->count && j < k; j++) {
            if (ground_truth->neighbors[j].id == aid) {
                hits++;
                break;
            }
        }
    }
    return (float)hits / (float)kk;
}

/* L5: Precision@K — fraction of returned results that are relevant.
 * precision = |approx ∩ ground_truth| / |approx|
 * Important when returning more than k candidates (oversampling). */
float eval_precision_at_k(const KNNResult *ground_truth,
                           const KNNResult *approx, int k) {
    if (!approx || approx->count <= 0) return 0.0f;
    int kk = approx->count < k ? approx->count : k;
    if (kk <= 0) return 0.0f;

    int hits = 0;
    for (int i = 0; i < kk; i++) {
        int aid = approx->neighbors[i].id;
        for (int j = 0; j < ground_truth->count; j++) {
            if (ground_truth->neighbors[j].id == aid) {
                hits++;
                break;
            }
        }
    }
    return (float)hits / (float)kk;
}

/* L5: Mean Average Precision (mAP).
 * AP = Σ(P(k) × rel(k)) / |relevant|  where rel(k)=1 if item k is relevant.
 * mAP = mean AP over all queries.
 * This is the standard metric in information retrieval benchmarks. */
float eval_average_precision(const KNNResult *ground_truth,
                              const KNNResult *approx) {
    if (!ground_truth || !approx || ground_truth->count <= 0) return 0.0f;

    float sum_precision = 0.0f;
    int hits = 0;

    for (int i = 0; i < approx->count; i++) {
        int aid = approx->neighbors[i].id;
        int is_relevant = 0;
        for (int j = 0; j < ground_truth->count; j++) {
            if (ground_truth->neighbors[j].id == aid) {
                is_relevant = 1;
                break;
            }
        }
        if (is_relevant) {
            hits++;
            sum_precision += (float)hits / (float)(i + 1);
        }
    }

    int n_relevant = ground_truth->count;
    return n_relevant > 0 ? sum_precision / n_relevant : 0.0f;
}

/* L5: nDCG@K — Normalized Discounted Cumulative Gain.
 *
 * DCG@K = Σ_{i=1}^{K} rel_i / log₂(i+1)
 * nDCG@K = DCG@K / IDCG@K
 *
 * rel_i is binary here (1 if in ground truth, 0 otherwise).
 * The log₂(i+1) factor discounts relevance by rank position. */
float eval_ndcg_at_k(const KNNResult *ground_truth,
                      const KNNResult *approx, int k) {
    if (!ground_truth || !approx) return 0.0f;

    /* Compute DCG */
    float dcg = 0.0f;
    int kk = approx->count < k ? approx->count : k;
    for (int i = 0; i < kk; i++) {
        int aid = approx->neighbors[i].id;
        float rel = 0.0f;
        for (int j = 0; j < ground_truth->count; j++) {
            if (ground_truth->neighbors[j].id == aid) {
                rel = 1.0f;
                break;
            }
        }
        float discount = log2f((float)(i + 2));
        dcg += rel / discount;
    }

    /* Compute IDCG (ideal — all top-K relevant) */
    float idcg = 0.0f;
    int max_rel = ground_truth->count < kk ? ground_truth->count : kk;
    for (int i = 0; i < max_rel; i++) {
        idcg += 1.0f / log2f((float)(i + 2));
    }

    return idcg > 1e-10f ? dcg / idcg : 0.0f;
}

/* L7: Comprehensive evaluation across multiple queries.
 * Computes and prints all metrics for a batch of queries.
 * ground_truth[q] and approx[q] must be pre-computed results. */
void eval_batch_metrics(const KNNResult *ground_truth,
                        const KNNResult *approx,
                        int n_queries, int k) {
    if (!ground_truth || !approx || n_queries <= 0) return;

    float avg_recall = 0.0f, avg_precision = 0.0f;
    float avg_map = 0.0f, avg_ndcg = 0.0f;

    for (int q = 0; q < n_queries; q++) {
        avg_recall   += eval_recall_at_k(&ground_truth[q], &approx[q], k);
        avg_precision += eval_precision_at_k(&ground_truth[q], &approx[q], k);
        avg_map      += eval_average_precision(&ground_truth[q], &approx[q]);
        avg_ndcg     += eval_ndcg_at_k(&ground_truth[q], &approx[q], k);
    }

    printf("=== Evaluation Results (%d queries, K=%d) ===\n", n_queries, k);
    printf("  Recall@K:    %.4f\n", avg_recall / n_queries);
    printf("  Precision@K: %.4f\n", avg_precision / n_queries);
    printf("  mAP:         %.4f\n", avg_map / n_queries);
    printf("  nDCG@K:      %.4f\n", avg_ndcg / n_queries);
    printf("========================================\n");
}

/* L7: Benchmark throughput (QPS).
 * Measures queries per second for a given search function.
 * warmup_queries are excluded from timing. */
double eval_benchmark_qps(void (*search_fn)(const Vector*, int, KNNResult*),
                           const Vector *dataset, int n,
                           const Vector *queries, int n_queries,
                           int k, int warmup) {
    if (!search_fn || !queries || n_queries <= 0) return 0.0;

    KNNResult dummy;

    /* Warmup */
    for (int q = 0; q < warmup && q < n_queries; q++) {
        search_fn(&queries[q], k, &dummy);
    }

    /* Timed queries */
    clock_t start = clock();
    for (int q = warmup; q < n_queries; q++) {
        search_fn(&queries[q], k, &dummy);
    }
    clock_t end = clock();

    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    int timed_queries = n_queries - warmup;
    return timed_queries > 0 ? (double)timed_queries / elapsed : 0.0;
}

/* L7: Generate a Recall vs QPS tradeoff report.
 * Sweeps search parameters and reports recall/QPS pairs.
 * This is the standard visualization for ANN algorithm comparison.
 * (e.g., FAISS wiki plots, ann-benchmarks.com) */
void eval_recall_qps_sweep(const HNSWGraph *hnsw,
                            const IVFIndex *ivf,
                            const LSHTable *lsh,
                            const Vector *dataset, int n,
                            const Vector *queries, int n_queries,
                            const KNNResult *ground_truth, int k) {
    printf("\n=== Recall vs QPS Sweep ===\n");
    printf("%-15s %12s %12s %12s\n", "Index/Params", "Recall@K", "QPS", "Time(ms)");
    printf("%-15s %12s %12s %12s\n", "-----------", "--------", "---", "-------");

    /* HNSW: sweep ef_search */
    if (hnsw && hnsw->num_nodes > 0) {
        int ef_values[] = {8, 16, 32, 64, 128, 256};
        for (int ei = 0; ei < 6; ei++) {
            int ef = ef_values[ei];
            if (ef < k) ef = k * 2;

            float total_recall = 0.0f;
            clock_t t0 = clock();
            for (int q = 0; q < n_queries; q++) {
                KNNResult r;
                hnsw_search(hnsw, &queries[q], k, ef, &r);
                total_recall += eval_recall_at_k(&ground_truth[q], &r, k);
            }
            clock_t t1 = clock();
            double ms = (double)(t1 - t0) / n_queries / CLOCKS_PER_SEC * 1000.0;
            double qps = ms > 0.001 ? 1000.0 / ms : 0.0;
            printf("  HNSW ef=%-7d %12.4f %12.0f %12.4f\n",
                   ef, total_recall / n_queries, qps, ms);
        }
    }

    /* IVF: sweep nprobe */
    if (ivf && ivf->trained) {
        for (int np = 1; np <= 32; np *= 2) {
            float total_recall = 0.0f;
            clock_t t0 = clock();
            for (int q = 0; q < n_queries; q++) {
                KNNResult r;
                ivf_search(ivf, &queries[q], k, np, &r);
                total_recall += eval_recall_at_k(&ground_truth[q], &r, k);
            }
            clock_t t1 = clock();
            double ms = (double)(t1 - t0) / n_queries / CLOCKS_PER_SEC * 1000.0;
            double qps = ms > 0.001 ? 1000.0 / ms : 0.0;
            printf("  IVF  nprobe=%-5d %12.4f %12.0f %12.4f\n",
                   np, total_recall / n_queries, qps, ms);
        }
    }

    /* LSH: single point */
    if (lsh && lsh->num_vectors > 0) {
        float total_recall = 0.0f;
        clock_t t0 = clock();
        for (int q = 0; q < n_queries; q++) {
            KNNResult r;
            lsh_search(lsh, &queries[q], k, 1, &r);
            total_recall += eval_recall_at_k(&ground_truth[q], &r, k);
        }
        clock_t t1 = clock();
        double ms = (double)(t1 - t0) / n_queries / CLOCKS_PER_SEC * 1000.0;
        double qps = ms > 0.001 ? 1000.0 / ms : 0.0;
        printf("  LSH  (default)     %12.4f %12.0f %12.4f\n",
               total_recall / n_queries, qps, ms);
    }
    printf("==================================\n");
}

/* L7: Build-time benchmark.
 * Measures index construction time for each supported index type. */
void eval_build_benchmark(const Vector *vectors, int n,
                           int dim, int hnsw_M, int ivf_nlist) {
    printf("\n=== Index Build Benchmark (%d vectors, dim=%d) ===\n", n, dim);

    /* HNSW build */
    {
        HNSWGraph g;
        hnsw_init(&g, hnsw_M, 200);
        clock_t t0 = clock();
        for (int i = 0; i < n; i++) hnsw_insert(&g, &vectors[i], i);
        clock_t t1 = clock();
        printf("  HNSW (M=%d):     %.3f ms, %d nodes, %d edges (avg %.1f)\n",
               hnsw_M,
               (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0,
               g.num_nodes, 0, 0.0);
    }

    /* IVF build */
    {
        IVFIndex idx;
        ivf_init(&idx);
        clock_t t0 = clock();
        ivf_train(&idx, vectors, n, ivf_nlist);
        for (int i = 0; i < n; i++) ivf_add(&idx, &vectors[i], i);
        clock_t t1 = clock();
        printf("  IVF (nlist=%d):  %.3f ms\n",
               ivf_nlist, (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0);
    }

    /* LSH build */
    {
        LSHTable tbl;
        lsh_init(&tbl);
        clock_t t0 = clock();
        for (int i = 0; i < n; i++) lsh_insert(&tbl, &vectors[i], i);
        clock_t t1 = clock();
        printf("  LSH:             %.3f ms\n",
               (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0);
    }
    printf("===========================================\n");
}

/* L8: Statistical significance test for recall differences.
 * Uses paired t-test to determine if one index significantly
 * outperforms another on a set of queries.
 * Returns p-value (approximate for pedagogical purposes). */
float eval_paired_ttest(const float *recalls_a, const float *recalls_b,
                         int n) {
    if (n <= 1) return 1.0f;

    float mean_diff = 0.0f, var_diff = 0.0f;
    for (int i = 0; i < n; i++) {
        mean_diff += recalls_a[i] - recalls_b[i];
    }
    mean_diff /= n;

    for (int i = 0; i < n; i++) {
        float d = (recalls_a[i] - recalls_b[i]) - mean_diff;
        var_diff += d * d;
    }
    var_diff /= (n - 1);

    if (var_diff < 1e-15f) return (mean_diff < 1e-6f) ? 1.0f : 0.0f;

    float t_stat = mean_diff / sqrtf(var_diff / n);
    /* Crude p-value approximation using normal distribution */
    float abs_t = fabsf(t_stat);
    float p = 2.0f * (1.0f - 0.5f * (1.0f + erfcf(-abs_t / sqrtf(2.0f))));
    return p;
}