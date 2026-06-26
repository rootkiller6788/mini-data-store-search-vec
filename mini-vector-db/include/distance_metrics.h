#ifndef DISTANCE_METRICS_H
#define DISTANCE_METRICS_H

#include "vector_math.h"

/* L4: Metric space axioms — a set X with distance d: X×X→R satisfying
 *   (1) d(x,y) ≥ 0, d(x,y)=0 iff x=y  (non-negativity/identity)
 *   (2) d(x,y) = d(y,x)               (symmetry)
 *   (3) d(x,z) ≤ d(x,y) + d(y,z)      (triangle inequality)
 *
 * Inner product space: Euclidean distance derives from dot product norm
 * Cosine distance: violates triangle inequality on unnormalized vectors
 * Jaccard distance: defined on sets, satisfies all axioms for finite sets
 */

typedef enum {
    METRIC_L2 = 0,        /* Euclidean (L²) — from L² norm */
    METRIC_L1 = 1,        /* Manhattan (L¹) — from L¹ norm */
    METRIC_LINF = 2,      /* Chebyshev (L∞) — sup norm */
    METRIC_COSINE = 3,    /* 1 - cos(θ) — angular distance */
    METRIC_IP = 4,        /* Inner product (maximization) */
    METRIC_JACCARD = 5,   /* 1 - |A∩B|/|A∪B| — set similarity */
    METRIC_HAMMING = 6,   /* Σ[aᵢ≠bᵢ] — bit disagreement */
    METRIC_SQEUCLIDEAN = 7 /* Squared L² — faster, monotonic to L2 */
} MetricType;

/* L2: Metric selection — the choice of distance metric fundamentally
 * determines the geometry of the nearest-neighbor relationship.
 * For normalized embeddings, cosine ≈ Euclidean (up to a constant).
 * For unnormalized vectors (e.g., recommendation scores), IP is preferred.
 */

typedef float (*dist_func)(const Vector *a, const Vector *b);

typedef struct {
    MetricType type;
    const char *name;
    dist_func   compute;
    int         is_pseudo; /* 1 if violates metric axioms */
} DistanceMetric;

/* Retrieve a DistanceMetric descriptor by type */
const DistanceMetric *metric_get(MetricType type);

/* Compute distance between two vectors using specified metric */
float metric_distance(const Vector *a, const Vector *b, MetricType type);

/* Self-distance test: verifies d(a,a)=0 within tolerance */
int metric_check_identity(const Vector *a, MetricType type);

/* Symmetry test: verifies |d(a,b)-d(b,a)| < ε */
int metric_check_symmetry(const Vector *a, const Vector *b, MetricType type);

/* Triangle inequality test: d(a,c) ≤ d(a,b) + d(b,c) */
int metric_check_triangle(const Vector *a, const Vector *b, const Vector *c,
                           MetricType type);

/* Compute pairwise distance matrix between two vector sets
 * out[i*nb + j] = distance(a[i], b[j]) */
void metric_pairwise(const Vector *a, int na,
                     const Vector *b, int nb,
                     MetricType type, float *out);

/* Find the nearest neighbor in a dataset using specified metric */
int metric_nearest(const Vector *dataset, int n,
                   const Vector *query, MetricType type,
                   float *out_dist);

/* Convert a float vector to binary representation for Hamming distance
 * threshold: values > threshold → 1, else 0 */
void vec_to_binary(const Vector *v, unsigned char *bits, float threshold);

/* Hamming distance between two binary vectors */
int vec_hamming_dist(const unsigned char *a, const unsigned char *b, int num_bytes);

/* Jaccard distance between two binary vectors */
float vec_jaccard_dist(const unsigned char *a, const unsigned char *b, int num_bytes);

/* Chebyshev distance: max_i |a[i] - b[i]| */
float vec_chebyshev_dist(const Vector *a, const Vector *b);

/* Print metric name */
const char *metric_name(MetricType type);

#endif
