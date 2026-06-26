#ifndef VECTOR_MATH_H
#define VECTOR_MATH_H

#include <stddef.h>

#define DIM_MAX 128

typedef struct {
    float data[DIM_MAX];
    int   dim;
} Vector;

float vec_euclidean_dist(const Vector *a, const Vector *b);
float vec_manhattan_dist(const Vector *a, const Vector *b);
float vec_cosine_similarity(const Vector *a, const Vector *b);
float vec_cosine_dist(const Vector *a, const Vector *b);
float vec_dot_product(const Vector *a, const Vector *b);
float vec_inner_product(const Vector *a, const Vector *b);
float vec_sqeuclidean_dist(const Vector *a, const Vector *b);
float vec_l2_norm(const Vector *v);
void  vec_l2_normalize(Vector *v);
void  vec_add(const Vector *a, const Vector *b, Vector *out);
void  vec_sub(const Vector *a, const Vector *b, Vector *out);
void  vec_scale(Vector *v, float s);
void  vec_zero(Vector *v, int dim);
void  vec_copy(const Vector *src, Vector *dst);
void  vec_fill_random(Vector *v, int dim);
void  vec_fill_random_normal(Vector *v, int dim);
void  vec_print(const Vector *v);
void  vec_print_short(const Vector *v);

#endif
