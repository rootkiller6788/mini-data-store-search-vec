#ifndef PQ_FULL_H
#define PQ_FULL_H

#include "vector_math.h"
#include "ivf_pq.h"

/* L5: Full Product Quantization Implementation
 *
 * Product Quantization (Jegou et al., 2011) decomposes the vector space
 * into M orthogonal subspaces and quantizes each independently.
 *
 * This header adds:
 * - Asymmetric Distance Computation (ADC): query is NOT quantized
 * - Symmetric Distance Computation (SDC): both query and DB quantized
 * - PQ distance lookup tables for fast distance computation
 * - Full encode/decode roundtrip
 *
 * L4: Rate-Distortion Theory
 * PQ achieves (b/M)·log₂(ks) bits per dimension, where:
 *   b = bits per vector component (32 for float)
 *   M = number of subquantizers
 *   ks = codebook size per subquantizer
 *
 * Distortion ∝ ks^{-2/d'} where d' = d/M (subspace dimension)
 */

#define PQ_LOOKUP_MAX_M  16
#define PQ_LOOKUP_MAX_KS 256

typedef struct {
    float table[PQ_LOOKUP_MAX_M][PQ_LOOKUP_MAX_KS];
    int   M;
    int   ks;
    int   subdim;
} PQDistanceTable;

/* L2: Initialize distance lookup table from codebook and query vector.
 * For ADC: precompute distance from query_subvector to each codeword.
 * This reduces per-vector distance from O(d) to O(M) table lookups. */
void pq_compute_distance_table(const PQCodebook *cb,
                                const float *query_residual,
                                int M, int ks, int subdim,
                                PQDistanceTable *table);

/* Compute ADC distance using precomputed lookup table */
float pq_adc_distance(const PQDistanceTable *table,
                       const PQCode *code, int M);

/* Compute SDC distance: both vectors are quantized.
 * Requires precomputed codebook self-distance table. */
float pq_sdc_distance(const PQCodebook *cb,
                       const PQCode *code_a, const PQCode *code_b,
                       int M, int subdim);

/* Precompute symmetric distance table between all codeword pairs.
 * out[i][j] = ||c_i - c_j||²  for subspace m */
void pq_compute_sdc_table(const PQCodebook *cb, int m,
                           int ks, int subdim,
                           float (*out)[PQ_LOOKUP_MAX_KS]);

/* Encode a residual vector into PQ codes */
void pq_encode(const PQCodebook *cb, const float *residual,
                int dim, int M, PQCode *code);

/* Decode PQ codes back to approximate vector */
void pq_decode(const PQCodebook *cb, const PQCode *code,
                int M, int subdim, float *out);

/* L8: Polysemous Codes — use Hamming distance on PQ codes as a pre-filter.
 * If Hamming(c_q, c_db) > threshold, skip the full distance computation.
 * This exploits the observation that similar vectors have similar PQ codes. */
int pq_hamming_prefilter(const PQCode *code_a, const PQCode *code_b,
                          int M, int threshold);

/* Batch PQ encode: encode n vectors at once */
void pq_encode_batch(const PQCodebook *cb, const float *const *residuals,
                      int n, int dim, int M, PQCode *codes);

/* PQ code to string (for debugging) */
void pq_code_to_string(const PQCode *code, int M, char *buf, int buf_size);

#endif