#include "vector_math.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

float vec_euclidean_dist(const Vector *a, const Vector *b)
{
    float sum = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        float diff = a->data[i] - b->data[i];
        sum += diff * diff;
    }
    return sqrtf(sum);
}

float vec_manhattan_dist(const Vector *a, const Vector *b)
{
    float sum = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        sum += fabsf(a->data[i] - b->data[i]);
    }
    return sum;
}

float vec_cosine_similarity(const Vector *a, const Vector *b)
{
    float dot = 0.0f, mag_a = 0.0f, mag_b = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        dot   += a->data[i] * b->data[i];
        mag_a += a->data[i] * a->data[i];
        mag_b += b->data[i] * b->data[i];
    }
    if (mag_a == 0.0f || mag_b == 0.0f) return 0.0f;
    return dot / (sqrtf(mag_a) * sqrtf(mag_b));
}

float vec_cosine_dist(const Vector *a, const Vector *b)
{
    return 1.0f - vec_cosine_similarity(a, b);
}

float vec_dot_product(const Vector *a, const Vector *b)
{
    float dot = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        dot += a->data[i] * b->data[i];
    }
    return dot;
}

void vec_l2_normalize(Vector *v)
{
    float norm = 0.0f;
    for (int i = 0; i < v->dim; i++) {
        norm += v->data[i] * v->data[i];
    }
    norm = sqrtf(norm);
    if (norm > 1e-8f) {
        for (int i = 0; i < v->dim; i++) {
            v->data[i] /= norm;
        }
    }
}

float vec_l2_norm(const Vector *v)
{
    float norm = 0.0f;
    for (int i = 0; i < v->dim; i++) {
        norm += v->data[i] * v->data[i];
    }
    return sqrtf(norm);
}

void vec_add(const Vector *a, const Vector *b, Vector *out)
{
    int dim = a->dim < b->dim ? a->dim : b->dim;
    out->dim = dim;
    for (int i = 0; i < dim; i++) {
        out->data[i] = a->data[i] + b->data[i];
    }
}

void vec_sub(const Vector *a, const Vector *b, Vector *out)
{
    int dim = a->dim < b->dim ? a->dim : b->dim;
    out->dim = dim;
    for (int i = 0; i < dim; i++) {
        out->data[i] = a->data[i] - b->data[i];
    }
}

void vec_scale(Vector *v, float s)
{
    for (int i = 0; i < v->dim; i++) {
        v->data[i] *= s;
    }
}

void vec_zero(Vector *v, int dim)
{
    v->dim = dim;
    for (int i = 0; i < dim; i++) {
        v->data[i] = 0.0f;
    }
}

void vec_copy(const Vector *src, Vector *dst)
{
    dst->dim = src->dim;
    for (int i = 0; i < src->dim; i++) {
        dst->data[i] = src->data[i];
    }
}

void vec_fill_random(Vector *v, int dim)
{
    v->dim = dim;
    for (int i = 0; i < dim; i++) {
        v->data[i] = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    }
}

void vec_fill_random_normal(Vector *v, int dim)
{
    v->dim = dim;
    for (int i = 0; i < dim; i++) {
        float u1 = (float)rand() / (float)RAND_MAX;
        float u2 = (float)rand() / (float)RAND_MAX;
        float r = sqrtf(-2.0f * logf(u1 + 1e-10f));
        v->data[i] = r * cosf(2.0f * 3.14159265f * u2);
    }
}

void vec_print(const Vector *v)
{
    printf("[");
    for (int i = 0; i < v->dim; i++) {
        printf("%.4f", v->data[i]);
        if (i < v->dim - 1) printf(", ");
    }
    printf("]\n");
}

void vec_print_short(const Vector *v)
{
    if (v->dim <= 8) {
        vec_print(v);
        return;
    }
    printf("[");
    for (int i = 0; i < 4; i++) printf("%.4f, ", v->data[i]);
    printf("..., ");
    for (int i = v->dim - 4; i < v->dim; i++) {
        printf("%.4f", v->data[i]);
        if (i < v->dim - 1) printf(", ");
    }
    printf("] (dim=%d)\n", v->dim);
}

float vec_inner_product(const Vector *a, const Vector *b)
{
    return vec_dot_product(a, b);
}

float vec_sqeuclidean_dist(const Vector *a, const Vector *b)
{
    float sum = 0.0f;
    int dim = a->dim < b->dim ? a->dim : b->dim;
    for (int i = 0; i < dim; i++) {
        float diff = a->data[i] - b->data[i];
        sum += diff * diff;
    }
    return sum;
}
