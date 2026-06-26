#ifndef KMEANS_PP_H
#define KMEANS_PP_H

#include "vector_math.h"

int kmeans_pp_init(const Vector *vectors, int n, int dim, int k,
                   float (*centroids)[DIM_MAX]);
int kmeans_cluster(const Vector *vectors, int n, int dim, int k,
                   int max_iters, float (*centroids)[DIM_MAX],
                   int *assignments);
float kmeans_wcss(const Vector *vectors, int n, int dim,
                  const float (*centroids)[DIM_MAX], int k,
                  const int *assignments);
void kmeans_elbow(const Vector *vectors, int n, int dim, int max_k,
                  int max_iters, float *wcss_out);
float kmeans_silhouette(const Vector *vectors, int n, int dim,
                        const int *assignments, int k);
void kmeans_print_elbow(int max_k, const float *wcss);

#endif