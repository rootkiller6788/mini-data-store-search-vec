#include "pq_full.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* L5: Asymmetric Distance Computation (ADC)
 *
 * ADC: query vector is NOT quantized — only database vectors are.
 * This provides higher accuracy than SDC because the query retains
 * full precision. The tradeoff is that the query must compute distances
 * against all codewords (M × ks lookups).
 *
 * Precomputation: for each subquantizer m and each codeword k,
 * compute ||q_m - c_{m,k}||².
 * Then distance to any PQ-encoded vector: Σ_m table[m][code[m]].
 *
 * This reduces per-vector distance from O(d) to O(M).
 * With M=8, d=128, this is a 16× speedup over exact distance.
 */

/* L2: Build ADC lookup table from query residual and codebook.
 * table[m][k] = ||query_residual[subspace m] - codebook[m][k]||² */
void pq_compute_distance_table(const PQCodebook *cb,
                                const float *query_residual,
                                int M, int ks, int subdim,
                                PQDistanceTable *table) {
    table->M = M;
    table->ks = ks;
    table->subdim = subdim;

    for (int m = 0; m < M; m++) {
        int offset = m * subdim;
        for (int k = 0; k < ks; k++) {
            float dist = 0.0f;
            for (int d = 0; d < subdim; d++) {
                float diff = query_residual[offset + d] -
                             cb->subquantizers[m][k][d];
                dist += diff * diff;
            }
            table->table[m][k] = dist;
        }
    }
}

/* L2: ADC distance using precomputed lookup table.
 * Performs M table lookups and addition — very fast.
 * Complexity: O(M) vs O(d) for exact L2. */
float pq_adc_distance(const PQDistanceTable *table,
                       const PQCode *code, int M) {
    float total = 0.0f;
    for (int m = 0; m < M; m++) {
        int k = code->codes[m];
        if (k >= 0 && k < table->ks) {
            total += table->table[m][k];
        }
    }
    return total;
}

/* L5: Symmetric Distance Computation (SDC)
 *
 * SDC: both query AND database vectors are quantized.
 * This enables extremely fast distance computation using a precomputed
 * codebook self-distance table, at the cost of additional quantization
 * error on the query side.
 *
 * SDC is preferred when query latency must be absolutely minimized.
 * The additional error is bounded by the quantization distortion. */

void pq_compute_sdc_table(const PQCodebook *cb, int m,
                           int ks, int subdim,
                           float (*out)[PQ_LOOKUP_MAX_KS]) {
    for (int i = 0; i < ks; i++) {
        for (int j = 0; j < ks; j++) {
            float dist = 0.0f;
            for (int d = 0; d < subdim; d++) {
                float diff = cb->subquantizers[m][i][d] -
                             cb->subquantizers[m][j][d];
                dist += diff * diff;
            }
            out[i][j] = dist;
        }
    }
}

float pq_sdc_distance(const PQCodebook *cb,
                       const PQCode *code_a, const PQCode *code_b,
                       int M, int subdim) {
    float total = 0.0f;
    for (int m = 0; m < M; m++) {
        int ka = code_a->codes[m];
        int kb = code_b->codes[m];
        float dist = 0.0f;
        for (int d = 0; d < subdim; d++) {
            float diff = cb->subquantizers[m][ka][d] -
                         cb->subquantizers[m][kb][d];
            dist += diff * diff;
        }
        total += dist;
    }
    return total;
}

/* L5: PQ encoding — map residual vector to PQ codes.
 * For each subspace, find nearest codeword (exhaustive search).
 * Complexity: O(M·ks·subdim) = O(d·ks) per vector. */
void pq_encode(const PQCodebook *cb, const float *residual,
                int dim, int M, PQCode *code) {
    int subdim = dim / M;
    for (int m = 0; m < M; m++) {
        int offset = m * subdim;
        float best = FLT_MAX;
        int best_k = 0;
        for (int k = 0; k < PQ_KS; k++) {
            float dist = 0.0f;
            for (int d = 0; d < subdim; d++) {
                float diff = residual[offset + d] -
                             cb->subquantizers[m][k][d];
                dist += diff * diff;
            }
            if (dist < best) { best = dist; best_k = k; }
        }
        code->codes[m] = best_k;
    }
}

/* L5: PQ decoding — reconstruct approximate vector from codes.
 * out[dim] = concat(codebook[m][codes[m]] for m in 0..M-1)
 * The reconstruction error equals the quantization distortion. */
void pq_decode(const PQCodebook *cb, const PQCode *code,
                int M, int subdim, float *out) {
    for (int m = 0; m < M; m++) {
        int offset = m * subdim;
        int k = code->codes[m];
        for (int d = 0; d < subdim; d++) {
            out[offset + d] = cb->subquantizers[m][k][d];
        }
    }
}

/* L8: Polysemous Codes (Douze et al., 2016)
 *
 * Key insight: Similar vectors tend to have similar PQ codes.
 * We can use Hamming distance on PQ codes as a fast pre-filter:
 * if Hamming > threshold, skip the expensive ADC distance computation.
 *
 * This provides a 5-10× speedup with negligible recall loss when
 * threshold is chosen appropriately (typically M/4 to M/2). */
int pq_hamming_prefilter(const PQCode *code_a, const PQCode *code_b,
                          int M, int threshold) {
    int hamming = 0;
    for (int m = 0; m < M; m++) {
        /* Compare encoded codes — different code means subspace differs */
        if (code_a->codes[m] != code_b->codes[m]) {
            hamming++;
            if (hamming > threshold) return 0;
        }
    }
    return 1;
}

/* L5: Batch PQ encode — amortize function call overhead.
 * Processes n vectors, encoding each into M codes. */
void pq_encode_batch(const PQCodebook *cb, const float *const *residuals,
                      int n, int dim, int M, PQCode *codes) {
    for (int i = 0; i < n; i++) {
        pq_encode(cb, residuals[i], dim, M, &codes[i]);
    }
}

/* L5: Reconstruction error measurement.
 * Compares original vector with PQ-reconstructed vector.
 * Report both per-dimension and aggregate error. */
float pq_reconstruction_error(const float *original,
                               const float *reconstructed,
                               int dim) {
    float mse = 0.0f;
    for (int i = 0; i < dim; i++) {
        float diff = original[i] - reconstructed[i];
        mse += diff * diff;
    }
    return mse / dim;
}

/* Helper: PQ code to human-readable string */
void pq_code_to_string(const PQCode *code, int M, char *buf, int buf_size) {
    int pos = 0;
    for (int m = 0; m < M && pos < buf_size - 4; m++) {
        pos += snprintf(buf + pos, buf_size - pos, "%d%s",
                       code->codes[m], m < M-1 ? "," : "");
    }
}

/* L8: PQ code compression analysis.
 * Reports entropy of code distribution, useful for evaluating
 * whether codebook size is sufficient. */
void pq_code_entropy(const PQCode *codes, int n, int M, float *entropy) {
    for (int m = 0; m < M; m++) {
        int hist[PQ_KS] = {0};
        for (int i = 0; i < n; i++) {
            int k = codes[i].codes[m];
            if (k >= 0 && k < PQ_KS) hist[k]++;
        }
        float H = 0.0f;
        for (int k = 0; k < PQ_KS; k++) {
            if (hist[k] > 0) {
                float p = (float)hist[k] / n;
                H -= p * log2f(p);
            }
        }
        entropy[m] = H;
    }
}