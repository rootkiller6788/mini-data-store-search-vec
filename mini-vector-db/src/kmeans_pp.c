#include "vector_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* L5: k-means++ Initialization (Arthur & Vassilvitskii, 2007)
 *
 * Standard k-means with random initialization can produce arbitrarily bad
 * clusterings. k-means++ provides an O(log k)-competitive guarantee:
 *   E[cost] ≤ 8(ln k + 2) · OPT
 *
 * Algorithm:
 *   1. Choose first centroid uniformly at random
 *   2. For each subsequent centroid, sample with probability proportional
 *      to squared distance from nearest existing centroid (D² weighting)
 *   3. Proceed with standard Lloyd's iterations
 *
 * This replaces the naive random initialization in ivf_pq.c.
 */

/* L4: Lloyd's Algorithm (1957, published 1982)
 * The standard k-means is known as Lloyd's algorithm:
 *   1. Assignment step: assign each point to nearest centroid
 *   2. Update step: recompute centroid as mean of assigned points
 * This monotonically decreases the within-cluster sum of squares (WCSS).
 * Converges to local minimum in finite iterations.
 */

/* Compute squared Euclidean distance between a vector and a centroid */
static float dist_sq_to_centroid(const Vector *v, const float *centroid, int dim) {
    float sum = 0.0f;
    for (int i = 0; i < dim; i++) {
        float diff = v->data[i] - centroid[i];
        sum += diff * diff;
    }
    return sum;
}

/* Compute within-cluster sum of squares (WCSS) for a clustering.
 * WCSS = Σ_{c} Σ_{x∈c} ||x - μ_c||²
 * This is the objective function that k-means minimizes. */
float kmeans_wcss(const Vector *vectors, int n, int dim,
                  const float (*centroids)[DIM_MAX], int k,
                  const int *assignments) {
    float wcss = 0.0f;
    for (int i = 0; i < n; i++) {
        int c = assignments[i];
        float d2 = dist_sq_to_centroid(&vectors[i], centroids[c], dim);
        wcss += d2;
    }
    return wcss;
}

/* L5: k-means++ initialization of centroids.
 * centroids: output buffer [k][DIM_MAX]
 * vectors: input data [n]
 * Returns 0 on success, -1 on failure.
 *
 * D² weighting (step 2) uses cumulative probability for O(k·n) sampling.
 * This avoids the O(k²·n) of naive sampling. */
int kmeans_pp_init(const Vector *vectors, int n, int dim, int k,
                   float (*centroids)[DIM_MAX]) {
    if (!vectors || n <= 0 || k <= 0 || k > n || !centroids) return -1;

    /* Step 1: Choose first centroid uniformly */
    int first = rand() % n;
    for (int d = 0; d < dim; d++) {
        centroids[0][d] = vectors[first].data[d];
    }

    /* Track minimum squared distance to any chosen centroid */
    float *min_dists = (float *)malloc(n * sizeof(float));
    if (!min_dists) return -1;
    for (int i = 0; i < n; i++) {
        min_dists[i] = dist_sq_to_centroid(&vectors[i], centroids[0], dim);
    }

    /* Step 2: Choose remaining k-1 centroids */
    for (int c = 1; c < k; c++) {
        /* Compute total D² weight */
        float total_weight = 0.0f;
        for (int i = 0; i < n; i++) {
            total_weight += min_dists[i];
        }

        /* Handle edge case: all remaining points at distance 0 */
        if (total_weight < 1e-30f) {
            /* Choose any unchosen point */
            for (int i = 0; i < n; i++) {
                if (min_dists[i] > 0.0f || i == first) continue;
                int already_chosen = 0;
                for (int j = 0; j < c; j++) {
                    float d2 = dist_sq_to_centroid(&vectors[i], centroids[j], dim);
                    if (d2 < 1e-10f) { already_chosen = 1; break; }
                }
                if (!already_chosen) {
                    for (int d = 0; d < dim; d++) centroids[c][d] = vectors[i].data[d];
                    break;
                }
            }
            break;
        }

        /* Sample with D² weighting via cumulative distribution */
        float r = (float)rand() / (float)RAND_MAX * total_weight;
        float cum = 0.0f;
        int chosen = n - 1;
        for (int i = 0; i < n; i++) {
            cum += min_dists[i];
            if (cum >= r) { chosen = i; break; }
        }

        /* Set chosen centroid */
        for (int d = 0; d < dim; d++) {
            centroids[c][d] = vectors[chosen].data[d];
        }

        /* Update min distances */
        for (int i = 0; i < n; i++) {
            float d2 = dist_sq_to_centroid(&vectors[i], centroids[c], dim);
            if (d2 < min_dists[i]) min_dists[i] = d2;
        }
    }

    free(min_dists);
    return 0;
}

/* L5: Standard k-means clustering with Lloyd's iterations.
 * Uses k-means++ initialization for quality guarantee.
 * assignments: output buffer [n] of cluster indices
 * Returns number of iterations until convergence. */
int kmeans_cluster(const Vector *vectors, int n, int dim, int k,
                   int max_iters, float (*centroids)[DIM_MAX],
                   int *assignments) {
    if (!vectors || !centroids || !assignments) return -1;

    /* Initialize with k-means++ */
    if (kmeans_pp_init(vectors, n, dim, k, centroids) != 0) return -1;

    int *counts = (int *)malloc(k * sizeof(int));
    float (*sums)[DIM_MAX] = (float (*)[DIM_MAX])calloc(k * DIM_MAX, sizeof(float));
    if (!counts || !sums) { free(counts); free(sums); return -1; }

    int iter;
    for (iter = 0; iter < max_iters; iter++) {
        int changed = 0;

        /* Assignment step */
        memset(counts, 0, k * sizeof(int));
        memset(sums, 0, k * DIM_MAX * sizeof(float));

        for (int i = 0; i < n; i++) {
            float best = FLT_MAX;
            int best_c = 0;
            for (int c = 0; c < k; c++) {
                float d2 = dist_sq_to_centroid(&vectors[i], centroids[c], dim);
                if (d2 < best) { best = d2; best_c = c; }
            }
            if (assignments[i] != best_c) {
                assignments[i] = best_c;
                changed++;
            }
            counts[best_c]++;
            for (int d = 0; d < dim; d++) {
                sums[best_c][d] += vectors[i].data[d];
            }
        }

        /* Update step */
        for (int c = 0; c < k; c++) {
            if (counts[c] > 0) {
                for (int d = 0; d < dim; d++) {
                    centroids[c][d] = sums[c][d] / (float)counts[c];
                }
            }
        }

        if (changed == 0) break;
    }

    free(counts);
    free(sums);
    return iter + 1;
}

/* L5: Elbow Method for selecting optimal k.
 * Computes WCSS for k = 1..max_k and returns the array.
 * The "elbow" is the k where WCSS reduction sharply diminishes.
 * This is a heuristic based on the tradeoff between model complexity
 * (k) and fit quality (WCSS). */
void kmeans_elbow(const Vector *vectors, int n, int dim, int max_k,
                  int max_iters, float *wcss_out) {
    if (!vectors || !wcss_out || max_k <= 0) return;

    for (int k = 1; k <= max_k && k <= n; k++) {
        float (*centroids)[DIM_MAX] = (float (*)[DIM_MAX])calloc(k * DIM_MAX, sizeof(float));
        int *assignments = (int *)calloc(n, sizeof(int));
        if (!centroids || !assignments) {
            wcss_out[k-1] = 0.0f;
            free(centroids); free(assignments);
            continue;
        }

        kmeans_cluster(vectors, n, dim, k, max_iters, centroids, assignments);
        wcss_out[k-1] = kmeans_wcss(vectors, n, dim,
                                     (const float (*)[DIM_MAX])centroids,
                                     k, assignments);

        free(centroids);
        free(assignments);
    }
}

/* L8: Silhouette Score — cluster quality metric (Rousseeuw, 1987)
 *
 * For each point i:
 *   a(i) = mean distance to points in own cluster
 *   b(i) = min mean distance to points in another cluster
 *   s(i) = (b(i) - a(i)) / max(a(i), b(i))
 *
 * s(i) ∈ [-1, 1]: 1 = well-clustered, 0 = boundary, -1 = misclustered.
 * Returns average silhouette score over all points. */
float kmeans_silhouette(const Vector *vectors, int n, int dim,
                        const int *assignments, int k) {
    if (!vectors || !assignments || n <= 0 || k <= 1) return 0.0f;

    float *a = (float *)calloc(n, sizeof(float));
    float *b = (float *)calloc(n, sizeof(float));
    int *cluster_sizes = (int *)calloc(k, sizeof(int));

    if (!a || !b || !cluster_sizes) {
        free(a); free(b); free(cluster_sizes);
        return 0.0f;
    }

    /* Compute cluster sizes */
    for (int i = 0; i < n; i++) {
        cluster_sizes[assignments[i]]++;
    }

    /* Compute a(i): mean intra-cluster distance */
    for (int i = 0; i < n; i++) {
        int ci = assignments[i];
        if (cluster_sizes[ci] <= 1) { a[i] = 0.0f; continue; }
        float sum = 0.0f;
        for (int j = 0; j < n; j++) {
            if (i == j || assignments[j] != ci) continue;
            sum += sqrtf(dist_sq_to_centroid(&vectors[i],
                        (const float *)&vectors[j].data[0], dim));
        }
        a[i] = sum / (cluster_sizes[ci] - 1);
    }

    /* Compute b(i): min mean inter-cluster distance */
    for (int i = 0; i < n; i++) {
        int ci = assignments[i];
        float min_mean = FLT_MAX;
        for (int c = 0; c < k; c++) {
            if (c == ci || cluster_sizes[c] == 0) continue;
            float sum = 0.0f;
            for (int j = 0; j < n; j++) {
                if (assignments[j] != c) continue;
                sum += sqrtf(dist_sq_to_centroid(&vectors[i],
                            (const float *)&vectors[j].data[0], dim));
            }
            float mean = sum / cluster_sizes[c];
            if (mean < min_mean) min_mean = mean;
        }
        b[i] = min_mean;
    }

    /* Compute average silhouette */
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        float denom = a[i] > b[i] ? a[i] : b[i];
        if (denom < 1e-10f) { total += 0.0f; continue; }
        total += (b[i] - a[i]) / denom;
    }

    free(a); free(b); free(cluster_sizes);
    return total / n;
}

/* Print elbow curve to stdout */
void kmeans_print_elbow(int max_k, const float *wcss) {
    printf("=== Elbow Method (WCSS vs k) ===\n");
    printf("  k      WCSS        Delta\n");
    printf("  ---    --------    --------\n");
    float prev = 0.0f;
    for (int k = 1; k <= max_k; k++) {
        float delta = (k > 1) ? prev - wcss[k-1] : 0.0f;
        printf("  %-6d %-11.2f %-9.2f\n", k, wcss[k-1], delta);
        prev = wcss[k-1];
    }
    printf("===============================\n");
}