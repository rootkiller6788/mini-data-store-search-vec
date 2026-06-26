#include "vector_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* L4: Dimensionality Reduction — Theoretical Foundations
 *
 * 1. Johnson-Lindenstrauss Lemma (1984):
 *    For any ε∈(0,1), set X of n points in R^d, there exists a linear
 *    map f: R^d → R^k with k = O(log n / ε²) such that for all x,y∈X:
 *      (1-ε)||x-y||² ≤ ||f(x)-f(y)||² ≤ (1+ε)||x-y||²
 *
 *    Proof uses random projection onto k Gaussian vectors.
 *    This is the theoretical basis for LSH and random projection hashing.
 *
 * 2. PCA (Pearson, 1901; Hotelling, 1933):
 *    Finds orthogonal directions of maximum variance.
 *    Optimal linear reconstruction in L² sense (Eckart-Young theorem).
 *    Uses eigendecomposition of covariance matrix.
 *
 * 3. Curse of Dimensionality (Bellman, 1961):
 *    As d→∞, all points become equidistant.
 *    Volume of hypersphere → 0 relative to enclosing hypercube.
 *    Distance-based indexing breaks down → motivates ANN.
 */

/* L2: Compute mean vector of dataset.
 * μ = (1/n) Σ x_i */
void dim_compute_mean(const Vector *vectors, int n, Vector *mean) {
    if (n <= 0 || !vectors || !mean) return;
    int dim = vectors[0].dim;
    vec_zero(mean, dim);
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < dim; d++) {
            mean->data[d] += vectors[i].data[d];
        }
    }
    for (int d = 0; d < dim; d++) {
        mean->data[d] /= (float)n;
    }
}

/* L2: Center vectors by subtracting mean.
 * x_i' = x_i - μ  (zero-mean normalization) */
void dim_center(Vector *vectors, int n, const Vector *mean) {
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < vectors[i].dim; d++) {
            vectors[i].data[d] -= mean->data[d];
        }
    }
}

/* L5: PCA via Power Iteration Method
 *
 * Computes the top k principal components without full eigendecomposition.
 * Each iteration: v_{t+1} = C·v_t / ||C·v_t||  where C is covariance.
 * Convergence rate: O((λ₂/λ₁)^t) — geometric.
 *
 * For k > 1, uses deflation: project out previous components.
 * Time: O(k·n·d·iterations) vs O(d³) for full eigendecomposition.
 *
 * This is the "online PCA" approach from Bishop's PRML. */
static float power_iteration(const float *cov, int dim,
                              float *eigenvector, int max_iter) {
    /* Initialize with random unit vector */
    for (int d = 0; d < dim; d++) {
        eigenvector[d] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    }
    float norm = 0.0f;
    for (int d = 0; d < dim; d++) norm += eigenvector[d] * eigenvector[d];
    norm = sqrtf(norm);
    if (norm > 1e-8f) {
        for (int d = 0; d < dim; d++) eigenvector[d] /= norm;
    }

    float eigenvalue = 0.0f;
    for (int iter = 0; iter < max_iter; iter++) {
        /* v = C·v */
        float new_vec[DIM_MAX] = {0};
        for (int i = 0; i < dim; i++) {
            for (int j = 0; j < dim; j++) {
                new_vec[i] += cov[i * dim + j] * eigenvector[j];
            }
        }

        /* Normalize */
        norm = 0.0f;
        for (int d = 0; d < dim; d++) norm += new_vec[d] * new_vec[d];
        norm = sqrtf(norm);
        if (norm < 1e-10f) return 0.0f;

        for (int d = 0; d < dim; d++) eigenvector[d] = new_vec[d] / norm;

        /* Rayleigh quotient for eigenvalue estimate */
        eigenvalue = 0.0f;
        for (int i = 0; i < dim; i++) {
            float ci = 0.0f;
            for (int j = 0; j < dim; j++) ci += cov[i * dim + j] * eigenvector[j];
            eigenvalue += eigenvector[i] * ci;
        }
    }
    return eigenvalue;
}

/* L5: Full PCA — returns top k eigenvectors and eigenvalues.
 * cov_matrix: [dim][dim] covariance matrix (output)
 * eigenvecs: [k][dim] principal components (output)
 * eigenvals: [k] eigenvalues in descending order (output)
 * Returns 0 on success. */
int dim_pca(const Vector *vectors, int n, int k,
            float *cov_matrix,
            float (*eigenvecs)[DIM_MAX],
            float *eigenvals) {
    if (!vectors || n <= 1 || k <= 0 || k > DIM_MAX) return -1;
    int dim = vectors[0].dim;
    if (k > dim) k = dim;

    /* Compute mean */
    Vector mean;
    dim_compute_mean(vectors, n, &mean);

    /* Compute covariance matrix: C[i][j] = Σ(x_i-μ_i)(x_j-μ_j) / (n-1) */
    for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
            float cov = 0.0f;
            for (int p = 0; p < n; p++) {
                cov += (vectors[p].data[i] - mean.data[i]) *
                       (vectors[p].data[j] - mean.data[j]);
            }
            cov_matrix[i * dim + j] = cov / (float)(n - 1);
        }
    }

    /* Power iteration with deflation for top k components */
    float *working_cov = (float *)malloc(dim * dim * sizeof(float));
    if (!working_cov) return -1;
    memcpy(working_cov, cov_matrix, dim * dim * sizeof(float));

    for (int c = 0; c < k; c++) {
        eigenvals[c] = power_iteration(working_cov, dim, eigenvecs[c], 50);

        /* Deflation: C' = C - λ·v·v^T */
        float lambda = eigenvals[c];
        for (int i = 0; i < dim; i++) {
            for (int j = 0; j < dim; j++) {
                working_cov[i * dim + j] -= lambda *
                    eigenvecs[c][i] * eigenvecs[c][j];
            }
        }
    }

    free(working_cov);
    return 0;
}

/* L5: Project vector onto PCA subspace.
 * Transforms d-dimensional vector to k-dimensional representation.
 * out[k] = W^T · (x - μ)  where W is [k×d] projection matrix. */
void dim_project_pca(const Vector *v, const Vector *mean,
                     const float (*eigenvecs)[DIM_MAX], int k,
                     float *out) {
    for (int c = 0; c < k; c++) {
        float proj = 0.0f;
        for (int d = 0; d < v->dim; d++) {
            proj += eigenvecs[c][d] * (v->data[d] - mean->data[d]);
        }
        out[c] = proj;
    }
}

/* L4: Random Projection — Johnson-Lindenstrauss Transform
 *
 * Creates a random matrix R ∈ R^{k×d} with entries from N(0, 1/k).
 * By JL lemma, with k = O(log n / ε²), pairwise distances are preserved
 * within (1±ε) factor with high probability.
 *
 * This is the foundation of LSH (see lsh.c) and random projection trees.
 * Simpler than PCA and data-oblivious (no training required). */
void dim_random_projection_matrix(float (*R)[DIM_MAX], int k, int d) {
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < d; j++) {
            /* Box-Muller: N(0,1) */
            float u1 = (float)rand() / (float)RAND_MAX;
            float u2 = (float)rand() / (float)RAND_MAX;
            float g = sqrtf(-2.0f * logf(u1 + 1e-10f)) *
                      cosf(2.0f * 3.14159265f * u2);
            R[i][j] = g / sqrtf((float)k);
        }
    }
}

/* L4: Apply random projection to a vector.
 * out = R · v  (k-dimensional output) */
void dim_project_random(const Vector *v, const float (*R)[DIM_MAX],
                         int k, float *out) {
    for (int i = 0; i < k; i++) {
        float dot = 0.0f;
        for (int d = 0; d < v->dim; d++) {
            dot += R[i][d] * v->data[d];
        }
        out[i] = dot;
    }
}

/* L4: Verify JL lemma distance preservation.
 * Checks that for all pairs, (1-ε)||a-b||² ≤ ||f(a)-f(b)||² ≤ (1+ε)||a-b||²
 * Returns the fraction of pairs that satisfy the guarantee. */
float dim_verify_jl(const Vector *vectors, int n,
                    const float (*R)[DIM_MAX], int k,
                    float epsilon) {
    if (n < 2) return 1.0f;
    int passed = 0, total = 0;
    float *proj = (float *)malloc(n * k * sizeof(float));
    if (!proj) return 0.0f;

    for (int i = 0; i < n; i++) {
        dim_project_random(&vectors[i], R, k, &proj[i * k]);
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            /* Original distance */
            float orig2 = vec_sqeuclidean_dist(&vectors[i], &vectors[j]);

            /* Projected distance */
            float proj2 = 0.0f;
            for (int d = 0; d < k; d++) {
                float diff = proj[i * k + d] - proj[j * k + d];
                proj2 += diff * diff;
            }

            /* Check bounds */
            float lo = (1.0f - epsilon) * orig2;
            float hi = (1.0f + epsilon) * orig2;
            if (proj2 >= lo && proj2 <= hi) passed++;
            total++;
        }
    }

    free(proj);
    return total > 0 ? (float)passed / (float)total : 0.0f;
}

/* L4: Curse of Dimensionality — empirical demonstration.
 * Computes the ratio of nearest-to-farthest neighbor distance.
 * As d increases, this ratio → 1 (all points equidistant).
 * Returns the average ratio over the dataset. */
float dim_curse_ratio(const Vector *vectors, int n) {
    if (n < 2) return 1.0f;
    float avg_ratio = 0.0f;
    for (int i = 0; i < n; i++) {
        float nearest = 1e30f, farthest = 0.0f;
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            float d = vec_euclidean_dist(&vectors[i], &vectors[j]);
            if (d < nearest) nearest = d;
            if (d > farthest) farthest = d;
        }
        if (farthest > 1e-10f) {
            avg_ratio += nearest / farthest;
        }
    }
    return avg_ratio / n;
}

/* L8: Explained variance ratio — how many components to keep?
 * Computes cumulative variance explained by top k components.
 * Common rule: keep components until cumulative ≥ 0.95. */
void dim_explained_variance(const float *eigenvals, int n_eigenvals,
                             float *cumulative) {
    float total = 0.0f;
    for (int i = 0; i < n_eigenvals; i++) {
        if (eigenvals[i] > 0.0f) total += eigenvals[i];
    }
    if (total < 1e-10f) {
        for (int i = 0; i < n_eigenvals; i++) cumulative[i] = 0.0f;
        return;
    }
    float cum = 0.0f;
    for (int i = 0; i < n_eigenvals; i++) {
        cum += eigenvals[i];
        cumulative[i] = cum / total;
    }
}

/* Print PCA analysis results */
void dim_print_pca_results(int k, const float *eigenvals,
                            const float *cumulative) {
    printf("=== PCA Analysis ===\n");
    printf("  PC    Eigenvalue    Variance%%    Cumulative%%\n");
    printf("  ---   ----------    ---------    -----------\n");
    for (int i = 0; i < k; i++) {
        printf("  %-5d %-12.4f %-12.4f %-12.4f\n",
               i+1, eigenvals[i],
               eigenvals[i] / (cumulative ? (cumulative[k-1] > 0.001f ?
                cumulative[k-1] : 1.0f) : 1.0f) * 100.0f,
               cumulative ? cumulative[i] * 100.0f : 0.0f);
    }
    printf("=====================\n");
}

/* L8: Standardize vectors to z-scores: (x - μ) / σ
 * Makes features comparable and improves PCA quality on
 * heterogeneous data. */
void dim_standardize(Vector *vectors, int n) {
    if (n <= 0 || !vectors) return;
    int dim = vectors[0].dim;

    Vector mean;
    dim_compute_mean(vectors, n, &mean);

    /* Compute std dev per dimension */
    float *std_dev = (float *)calloc(dim, sizeof(float));
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < dim; d++) {
            float diff = vectors[i].data[d] - mean.data[d];
            std_dev[d] += diff * diff;
        }
    }
    for (int d = 0; d < dim; d++) {
        std_dev[d] = sqrtf(std_dev[d] / n);
        if (std_dev[d] < 1e-8f) std_dev[d] = 1.0f;
    }

    /* Apply standardization */
    for (int i = 0; i < n; i++) {
        for (int d = 0; d < dim; d++) {
            vectors[i].data[d] = (vectors[i].data[d] - mean.data[d]) / std_dev[d];
        }
    }

    free(std_dev);
}