#include "ivf_pq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

static float vec_l2_sq(const float *a, const float *b, int d)
{
    float s = 0.0f;
    for (int i = 0; i < d; i++) {
        float diff = a[i] - b[i];
        s += diff * diff;
    }
    return s;
}

static void vec_copy_raw(const float *src, float *dst, int d)
{
    for (int i = 0; i < d; i++) dst[i] = src[i];
}

static void vec_add_raw(const float *a, const float *b, float *out, int d)
{
    for (int i = 0; i < d; i++) out[i] = a[i] + b[i];
}

static void vec_scale_raw(float *v, float s, int d)
{
    for (int i = 0; i < d; i++) v[i] *= s;
}

void ivf_init(IVFIndex *index)
{
    index->kmeans.n_centroids = 0;
    index->kmeans.n_iters = 10;
    index->num_vectors = 0;
    index->trained = 0;
    for (int i = 0; i < IVF_MAX_CENTROIDS; i++) {
        index->lists[i].list_size = 0;
    }
    memset(index->kmeans.centers, 0, sizeof(index->kmeans.centers));
}

static void kmeans_init_centroids(KMeans *km, const Vector *vectors,
                                  int n, int nlist, int dim)
{
    int *chosen = (int *)calloc(nlist, sizeof(int));
    if (!chosen) return;

    for (int i = 0; i < nlist; i++) {
        int idx = rand() % n;
        chosen[i] = idx;
        for (int d = 0; d < dim; d++) {
            km->centers[i][d] = vectors[idx].data[d];
        }
    }
    free(chosen);

    for (int iter = 0; iter < km->n_iters; iter++) {
        int *counts = (int *)calloc(nlist, sizeof(int));
        float (*sums)[DIM_MAX] = (float (*)[DIM_MAX])calloc(nlist * DIM_MAX, sizeof(float));
        if (!counts || !sums) { free(counts); free(sums); return; }

        for (int i = 0; i < n; i++) {
            float best = 1e30f;
            int best_c = 0;
            for (int c = 0; c < nlist; c++) {
                float d = vec_l2_sq(vectors[i].data, km->centers[c], dim);
                if (d < best) { best = d; best_c = c; }
            }
            counts[best_c]++;
            for (int d = 0; d < dim; d++) {
                sums[best_c][d] += vectors[i].data[d];
            }
        }

        for (int c = 0; c < nlist; c++) {
            if (counts[c] > 0) {
                for (int d = 0; d < dim; d++) {
                    km->centers[c][d] = sums[c][d] / (float)counts[c];
                }
            }
        }
        free(counts);
        free(sums);
    }

    km->n_centroids = nlist;
}

static void train_pq_codebook(PQCodebook *cb, const float **residuals,
                              int n_residuals, int dim, int M, int ks)
{
    int subdim = dim / M;
    cb->n_subquantizers = M;

    for (int m = 0; m < M; m++) {
        int offset = m * subdim;

        float (*clusters)[PQ_SUBDIM] =
            (float (*)[PQ_SUBDIM])calloc(ks * PQ_SUBDIM, sizeof(float));
        int *counts = (int *)calloc(ks, sizeof(int));

        for (int k = 0; k < ks && k < ks; k++) {
            int ridx = rand() % n_residuals;
            for (int d = 0; d < subdim; d++) {
                clusters[k][d] = residuals[ridx][offset + d];
            }
        }

        for (int iter = 0; iter < 5; iter++) {
            memset(counts, 0, ks * sizeof(int));
            float (*sums)[PQ_SUBDIM] =
                (float (*)[PQ_SUBDIM])calloc(ks * PQ_SUBDIM, sizeof(float));

            for (int i = 0; i < n_residuals; i++) {
                float best = 1e30f;
                int best_k = 0;
                for (int k = 0; k < ks; k++) {
                    float sd = 0.0f;
                    for (int d = 0; d < subdim; d++) {
                        float diff = residuals[i][offset + d] - clusters[k][d];
                        sd += diff * diff;
                    }
                    if (sd < best) { best = sd; best_k = k; }
                }
                counts[best_k]++;
                for (int d = 0; d < subdim; d++) {
                    sums[best_k][d] += residuals[i][offset + d];
                }
            }

            for (int k = 0; k < ks; k++) {
                if (counts[k] > 0) {
                    for (int d = 0; d < subdim; d++) {
                        clusters[k][d] = sums[k][d] / (float)counts[k];
                    }
                }
            }
            free(sums);
        }

        for (int k = 0; k < ks; k++) {
            for (int d = 0; d < subdim; d++) {
                cb->subquantizers[m][k][d] = clusters[k][d];
            }
        }

        free(clusters);
        free(counts);
    }
}

void ivf_train(IVFIndex *index, const Vector *vectors, int n, int nlist)
{
    int dim = vectors[0].dim;
    kmeans_init_centroids(&index->kmeans, vectors, n, nlist, dim);

    float **residuals = (float **)malloc(n * sizeof(float *));
    for (int i = 0; i < n; i++) {
        residuals[i] = (float *)malloc(dim * sizeof(float));
        float best = 1e30f;
        int best_c = 0;
        for (int c = 0; c < nlist; c++) {
            float d = vec_l2_sq(vectors[i].data,
                                index->kmeans.centers[c], dim);
            if (d < best) { best = d; best_c = c; }
        }
        for (int d = 0; d < dim; d++) {
            residuals[i][d] = vectors[i].data[d] -
                              index->kmeans.centers[best_c][d];
        }
    }

    train_pq_codebook(&index->codebook, (const float **)residuals,
                      n, dim, PQ_M, PQ_KS);

    for (int i = 0; i < n; i++) free(residuals[i]);
    free(residuals);

    index->trained = 1;
}

void ivf_add(IVFIndex *index, const Vector *vec, int id)
{
    if (index->num_vectors >= IVF_MAX_VECTORS) return;
    int dim = vec->dim;
    int nlist = index->kmeans.n_centroids;

    float best = 1e30f;
    int best_c = 0;
    for (int c = 0; c < nlist; c++) {
        float d = vec_l2_sq(vec->data, index->kmeans.centers[c], dim);
        if (d < best) { best = d; best_c = c; }
    }

    float residual[DIM_MAX];
    for (int d = 0; d < dim; d++) {
        residual[d] = vec->data[d] - index->kmeans.centers[best_c][d];
    }

    int subdim = dim / PQ_M;
    int idx = index->num_vectors;
    index->vectors[idx] = *vec;
    index->id_map[idx] = id;

    for (int m = 0; m < PQ_M; m++) {
        int offset = m * subdim;
        float best_s = 1e30f;
        int best_k = 0;
        for (int k = 0; k < PQ_KS; k++) {
            float sd = 0.0f;
            for (int d = 0; d < subdim; d++) {
                float diff = residual[offset + d] -
                             index->codebook.subquantizers[m][k][d];
                sd += diff * diff;
            }
            if (sd < best_s) { best_s = sd; best_k = k; }
        }
        index->codes[idx].codes[m] = best_k;
    }

    InvertedList *list = &index->lists[best_c];
    list->list_ids[list->list_size++] = idx;

    index->num_vectors++;
}

static float pq_distance(const PQCodebook *cb, const PQCode *c,
                         const float *query_residual)
{
    float total = 0.0f;
    for (int m = 0; m < PQ_M; m++) {
        int offset = m * PQ_SUBDIM;
        int k = c->codes[m];
        float sd = 0.0f;
        for (int d = 0; d < PQ_SUBDIM; d++) {
            float diff = query_residual[offset + d] -
                         cb->subquantizers[m][k][d];
            sd += diff * diff;
        }
        total += sd;
    }
    return total;
}

void ivf_search(const IVFIndex *index, const Vector *query,
                int k, int nprobe, KNNResult *result)
{
    knn_result_init(result, k);
    if (!index->trained) return;

    int dim = query->dim;
    int nlist = index->kmeans.n_centroids;

    float centroid_dists[IVF_MAX_CENTROIDS];
    int   centroid_idx[IVF_MAX_CENTROIDS];
    for (int c = 0; c < nlist; c++) {
        centroid_dists[c] = vec_l2_sq(query->data,
                                       index->kmeans.centers[c], dim);
        centroid_idx[c] = c;
    }

    for (int i = 0; i < nlist - 1; i++) {
        for (int j = 0; j < nlist - i - 1; j++) {
            if (centroid_dists[j] > centroid_dists[j + 1]) {
                float td = centroid_dists[j];
                centroid_dists[j] = centroid_dists[j + 1];
                centroid_dists[j + 1] = td;
                int ti = centroid_idx[j];
                centroid_idx[j] = centroid_idx[j + 1];
                centroid_idx[j + 1] = ti;
            }
        }
    }

    KNNResult candidates;
    knn_result_init(&candidates, k * nprobe * 10);

    for (int p = 0; p < nprobe && p < nlist; p++) {
        int cid = centroid_idx[p];
        InvertedList *list = &index->lists[cid];

        float query_residual[DIM_MAX];
        for (int d = 0; d < dim; d++) {
            query_residual[d] = query->data[d] -
                                index->kmeans.centers[cid][d];
        }

        for (int i = 0; i < list->list_size; i++) {
            int vidx = list->list_ids[i];
            float dist = pq_distance(&index->codebook,
                                     &index->codes[vidx],
                                     query_residual);
            knn_result_add(&candidates, index->id_map[vidx], dist);
        }
    }

    knn_result_sort(&candidates);
    candidates.count = candidates.count < k ? candidates.count : k;

    for (int i = 0; i < candidates.count; i++) {
        result->neighbors[i] = candidates.neighbors[i];
        result->count++;
    }
}

void ivf_print_stats(const IVFIndex *index)
{
    printf("=== IVF-PQ Index Statistics ===\n");
    printf("  Centroids:     %d\n", index->kmeans.n_centroids);
    printf("  Trained:       %s\n", index->trained ? "yes" : "no");
    printf("  Vectors:       %d\n", index->num_vectors);
    printf("  Subquantizers: %d\n", PQ_M);
    printf("  KS per sub:    %d\n", PQ_KS);

    int non_empty = 0;
    int max_list = 0;
    for (int i = 0; i < index->kmeans.n_centroids; i++) {
        if (index->lists[i].list_size > 0) non_empty++;
        if (index->lists[i].list_size > max_list)
            max_list = index->lists[i].list_size;
    }
    printf("  Non-empty lists: %d\n", non_empty);
    printf("  Max list size:   %d\n", max_list);
    printf("===============================\n");
}
