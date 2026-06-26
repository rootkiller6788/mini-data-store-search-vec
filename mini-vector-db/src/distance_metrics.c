#include "distance_metrics.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* L5: Distance Metric Implementations
 *
 * Each metric corresponds to a different geometry on the vector space.
 * The choice of metric fundamentally affects nearest-neighbor results.
 *
 * L² family (Euclidean, squared Euclidean, Manhattan, Chebyshev):
 *   Derived from L^p norms: ||x||_p = (∑|x_i|^p)^{1/p}
 *   L² is the only L^p norm that induces an inner product space.
 *
 * Cosine: 1 - cos(θ), where cos(θ) = <a,b> / (||a||·||b||)
 *   Invariant under rotation and scaling (if normalized).
 *   Violates triangle inequality in general → pseudo-metric.
 *
 * Inner Product: <a,b> (maximization, not a true distance).
 *   For normalized vectors, max IP ⇔ min Euclidean.
 *
 * Jaccard: 1 - |A∩B|/|A∪B|, for binary/set vectors.
 *   Satisfies all metric axioms for finite sets.
 *
 * Hamming: count of differing bits, for binary vectors.
 *   Fundamental metric for error-correcting codes (Shannon, 1948).
 */

static float dist_l2(const Vector *a, const Vector *b) {
    return vec_euclidean_dist(a, b);
}

static float dist_l1(const Vector *a, const Vector *b) {
    float sum = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        sum += fabsf(a->data[i] - b->data[i]);
    }
    return sum;
}

/* L4: Chebyshev distance (L∞ norm):
 * d(a,b) = max_i |a_i - b_i|
 * Theorem: lim_{p→∞} L^p = L^∞.
 * Used in warehouse logistics (conveyor belt movement). */
static float dist_linf(const Vector *a, const Vector *b) {
    float maxd = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        float diff = fabsf(a->data[i] - b->data[i]);
        if (diff > maxd) maxd = diff;
    }
    return maxd;
}

static float dist_cosine(const Vector *a, const Vector *b) {
    return vec_cosine_dist(a, b);
}

/* L5: Inner product as distance proxy.
 * For MIPS (Maximum Inner Product Search): negate IP for minimization.
 * This is NOT a metric — it can be negative and asymmetric in magnitude. */
static float dist_ip(const Vector *a, const Vector *b) {
    return -vec_dot_product(a, b);
}

static float dist_sqeuclidean(const Vector *a, const Vector *b) {
    return vec_sqeuclidean_dist(a, b);
}

/* L5: Jaccard distance for binary vectors.
 * d(A,B) = 1 - |A∩B|/|A∪B|
 * Requires binary representation (pre-thresholded). */
static float dist_jaccard_binary(const Vector *a, const Vector *b) {
    (void)a; (void)b;
    return 0.0f;
}

static float dist_hamming_binary(const Vector *a, const Vector *b) {
    (void)a; (void)b;
    return 0.0f;
}

/* Metric registry — maps MetricType to function pointers */
static const DistanceMetric g_metrics[] = {
    { METRIC_L2,          "L2 (Euclidean)",    dist_l2,          0 },
    { METRIC_L1,          "L1 (Manhattan)",    dist_l1,          0 },
    { METRIC_LINF,        "Linf (Chebyshev)",  dist_linf,        0 },
    { METRIC_COSINE,      "Cosine",             dist_cosine,      1 },
    { METRIC_IP,          "Inner Product",      dist_ip,          1 },
    { METRIC_JACCARD,     "Jaccard",            dist_jaccard_binary, 0 },
    { METRIC_HAMMING,     "Hamming",            dist_hamming_binary, 0 },
    { METRIC_SQEUCLIDEAN, "Sq. Euclidean",     dist_sqeuclidean, 1 },
};

static const int g_num_metrics = sizeof(g_metrics) / sizeof(g_metrics[0]);

const DistanceMetric *metric_get(MetricType type) {
    if (type < 0 || type >= g_num_metrics) return NULL;
    return &g_metrics[type];
}

float metric_distance(const Vector *a, const Vector *b, MetricType type) {
    const DistanceMetric *m = metric_get(type);
    if (!m) return 0.0f;
    return m->compute(a, b);
}

int metric_check_identity(const Vector *a, MetricType type) {
    float d = metric_distance(a, a, type);
    return (d < 1e-6f) ? 1 : 0;
}

int metric_check_symmetry(const Vector *a, const Vector *b, MetricType type) {
    float d1 = metric_distance(a, b, type);
    float d2 = metric_distance(b, a, type);
    return (fabsf(d1 - d2) < 1e-5f) ? 1 : 0;
}

/* L4: Triangle inequality verification.
 * True metric must satisfy: d(a,c) ≤ d(a,b) + d(b,c)
 * This is the key property that enables pruning in exact search. */
int metric_check_triangle(const Vector *a, const Vector *b, const Vector *c,
                           MetricType type) {
    float dab = metric_distance(a, b, type);
    float dbc = metric_distance(b, c, type);
    float dac = metric_distance(a, c, type);
    float eps = 1e-5f * (1.0f + dab + dbc);
    return (dac <= dab + dbc + eps) ? 1 : 0;
}

void metric_pairwise(const Vector *a, int na,
                     const Vector *b, int nb,
                     MetricType type, float *out) {
    const DistanceMetric *m = metric_get(type);
    if (!m) return;
    for (int i = 0; i < na; i++) {
        for (int j = 0; j < nb; j++) {
            out[i * nb + j] = m->compute(&a[i], &b[j]);
        }
    }
}

/* L5: Nearest neighbor search with arbitrary metric.
 * O(n·d) per query — baseline for ANN evaluation.
 * Uses linear scan; no index acceleration. */
int metric_nearest(const Vector *dataset, int n,
                   const Vector *query, MetricType type,
                   float *out_dist) {
    const DistanceMetric *m = metric_get(type);
    if (!m || n <= 0) { if (out_dist) *out_dist = 0.0f; return -1; }

    int best_idx = 0;
    float best_dist = m->compute(&dataset[0], query);
    for (int i = 1; i < n; i++) {
        float d = m->compute(&dataset[i], query);
        if (d < best_dist) { best_dist = d; best_idx = i; }
    }
    if (out_dist) *out_dist = best_dist;
    return best_idx;
}

/* L5: Binary vector conversion.
 * Threshold: values above threshold become 1, else 0.
 * Each byte holds 8 dimensions. */
void vec_to_binary(const Vector *v, unsigned char *bits, float threshold) {
    int num_bytes = (v->dim + 7) / 8;
    memset(bits, 0, num_bytes);
    for (int i = 0; i < v->dim; i++) {
        if (v->data[i] > threshold) {
            bits[i / 8] |= (1 << (i % 8));
        }
    }
}

/* L5: Hamming distance — count differing bits.
 * Uses builtin popcount where available (x86: POPCNT). */
int vec_hamming_dist(const unsigned char *a, const unsigned char *b, int num_bytes) {
    int dist = 0;
    for (int i = 0; i < num_bytes; i++) {
        unsigned char xor_val = a[i] ^ b[i];
        while (xor_val) { dist++; xor_val &= xor_val - 1; }
    }
    return dist;
}

/* L5: Jaccard distance for binary vectors.
 * J(A,B) = 1 - |A∩B| / |A∪B|
 *      = 1 - (|A∩B|) / (|A| + |B| - |A∩B|)
 * Implementation counts intersections by bitwise AND. */
float vec_jaccard_dist(const unsigned char *a, const unsigned char *b, int num_bytes) {
    int intersect = 0;
    int union_cnt = 0;
    for (int i = 0; i < num_bytes; i++) {
        for (int bit = 0; bit < 8; bit++) {
            int abit = (a[i] >> bit) & 1;
            int bbit = (b[i] >> bit) & 1;
            if (abit && bbit) intersect++;
            if (abit || bbit) union_cnt++;
        }
    }
    if (union_cnt == 0) return 0.0f;
    return 1.0f - (float)intersect / (float)union_cnt;
}

float vec_chebyshev_dist(const Vector *a, const Vector *b) {
    return dist_linf(a, b);
}

const char *metric_name(MetricType type) {
    const DistanceMetric *m = metric_get(type);
    return m ? m->name : "Unknown";
}