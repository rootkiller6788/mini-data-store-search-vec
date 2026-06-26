#ifndef VECTOR_MATH_H
#define VECTOR_MATH_H

#include <stddef.h>

#define DIM_MAX 128

typedef struct {
    float data[DIM_MAX];
    int   dim;
} Vector;

float vec_euclidean_dist(const Vector *a, const Vector *b);
float vec_cosine_similarity(const Vector *a, const Vector *b);
float vec_dot_product(const Vector *a, const Vector *b);
void  vec_l2_normalize(Vector *v);
void  vec_print(const Vector *v);

#endif
