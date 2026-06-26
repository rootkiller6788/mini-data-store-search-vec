#ifndef DIMENSIONALITY_H
#define DIMENSIONALITY_H

#include "vector_math.h"

void dim_compute_mean(const Vector *vectors, int n, Vector *mean);
void dim_center(Vector *vectors, int n, const Vector *mean);
int dim_pca(const Vector *vectors, int n, int k,
            float *cov_matrix,
            float (*eigenvecs)[DIM_MAX],
            float *eigenvals);
void dim_project_pca(const Vector *v, const Vector *mean,
                     const float (*eigenvecs)[DIM_MAX], int k,
                     float *out);
void dim_random_projection_matrix(float (*R)[DIM_MAX], int k, int d);
void dim_project_random(const Vector *v, const float (*R)[DIM_MAX],
                         int k, float *out);
float dim_verify_jl(const Vector *vectors, int n,
                    const float (*R)[DIM_MAX], int k,
                    float epsilon);
float dim_curse_ratio(const Vector *vectors, int n);
void dim_explained_variance(const float *eigenvals, int n_eigenvals,
                             float *cumulative);
void dim_print_pca_results(int k, const float *eigenvals,
                            const float *cumulative);
void dim_standardize(Vector *vectors, int n);

#endif