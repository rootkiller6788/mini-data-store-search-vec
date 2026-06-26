#ifndef INDEX_EVAL_H
#define INDEX_EVAL_H

#include "exact_knn.h"
#include "hnsw.h"
#include "ivf_pq.h"
#include "lsh.h"

float eval_recall_at_k(const KNNResult *ground_truth,
                        const KNNResult *approx, int k);
float eval_precision_at_k(const KNNResult *ground_truth,
                           const KNNResult *approx, int k);
float eval_average_precision(const KNNResult *ground_truth,
                              const KNNResult *approx);
float eval_ndcg_at_k(const KNNResult *ground_truth,
                      const KNNResult *approx, int k);
void eval_batch_metrics(const KNNResult *ground_truth,
                        const KNNResult *approx,
                        int n_queries, int k);
double eval_benchmark_qps(void (*search_fn)(const Vector*, int, KNNResult*),
                           const Vector *dataset, int n,
                           const Vector *queries, int n_queries,
                           int k, int warmup);
void eval_recall_qps_sweep(const HNSWGraph *hnsw,
                            const IVFIndex *ivf,
                            const LSHTable *lsh,
                            const Vector *dataset, int n,
                            const Vector *queries, int n_queries,
                            const KNNResult *ground_truth, int k);
void eval_build_benchmark(const Vector *vectors, int n,
                           int dim, int hnsw_M, int ivf_nlist);
float eval_paired_ttest(const float *recalls_a, const float *recalls_b,
                         int n);

#endif