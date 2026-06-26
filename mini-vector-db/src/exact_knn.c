#include "exact_knn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

typedef struct {
    Neighbor data[KNN_MAX_K * 4];
    int      size;
} MaxHeap;

static void heap_swap(Neighbor *a, Neighbor *b)
{
    Neighbor t = *a;
    *a = *b;
    *b = t;
}

static void max_heap_push(MaxHeap *h, int id, float dist)
{
    if (h->size >= KNN_MAX_K * 4) return;
    h->data[h->size].id = id;
    h->data[h->size].distance = dist;
    int i = h->size;
    h->size++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->data[p].distance >= h->data[i].distance) break;
        heap_swap(&h->data[p], &h->data[i]);
        i = p;
    }
}

static void max_heap_pop(MaxHeap *h)
{
    if (h->size <= 0) return;
    h->size--;
    h->data[0] = h->data[h->size];
    int i = 0;
    while (1) {
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        int largest = i;
        if (left  < h->size && h->data[left].distance  > h->data[largest].distance) largest = left;
        if (right < h->size && h->data[right].distance > h->data[largest].distance) largest = right;
        if (largest == i) break;
        heap_swap(&h->data[i], &h->data[largest]);
        i = largest;
    }
}

static int compare_neighbor(const void *a, const void *b)
{
    float da = ((const Neighbor *)a)->distance;
    float db = ((const Neighbor *)b)->distance;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

void knn_brute_force(const Vector *dataset, int n,
                     const Vector *query, int k,
                     KNNResult *result)
{
    MaxHeap heap = {0};
    result->k = k;
    result->count = 0;

    for (int i = 0; i < n; i++) {
        float dist = vec_euclidean_dist(&dataset[i], query);
        max_heap_push(&heap, i, dist);
        if (heap.size > k) {
            max_heap_pop(&heap);
        }
    }

    qsort(heap.data, heap.size, sizeof(Neighbor), compare_neighbor);

    for (int i = 0; i < heap.size && i < k; i++) {
        result->neighbors[i] = heap.data[i];
        result->count++;
    }
}

KNNResult knn_search(const Vector *dataset, int n,
                     const Vector *query, int k)
{
    KNNResult result;
    knn_brute_force(dataset, n, query, k, &result);
    return result;
}

void knn_print_result(const KNNResult *result)
{
    printf("KNN Result (k=%d, found=%d):\n", result->k, result->count);
    for (int i = 0; i < result->count; i++) {
        printf("  rank=%2d  id=%6d  distance=%.6f\n",
               i + 1, result->neighbors[i].id, result->neighbors[i].distance);
    }
}

float knn_recall_at_k(const KNNResult *ground_truth,
                      const KNNResult *approx,
                      int k)
{
    int hits = 0;
    for (int i = 0; i < approx->count && i < k; i++) {
        int aid = approx->neighbors[i].id;
        for (int j = 0; j < ground_truth->count && j < k; j++) {
            if (ground_truth->neighbors[j].id == aid) {
                hits++;
                break;
            }
        }
    }
    return (float)hits / (float)k;
}

int knn_intersect_count(const KNNResult *a, const KNNResult *b)
{
    int count = 0;
    for (int i = 0; i < a->count; i++) {
        for (int j = 0; j < b->count; j++) {
            if (a->neighbors[i].id == b->neighbors[j].id) {
                count++;
                break;
            }
        }
    }
    return count;
}

void knn_result_init(KNNResult *r, int k)
{
    r->k = k;
    r->count = 0;
    memset(r->neighbors, 0, sizeof(r->neighbors));
}

void knn_result_sort(KNNResult *r)
{
    qsort(r->neighbors, r->count, sizeof(Neighbor), compare_neighbor);
}

void knn_result_add(KNNResult *r, int id, float dist)
{
    if (r->count >= KNN_MAX_K) return;
    r->neighbors[r->count].id = id;
    r->neighbors[r->count].distance = dist;
    r->count++;
}

void knn_result_prune(KNNResult *r, int k)
{
    if (r->count <= k) return;
    knn_result_sort(r);
    r->count = k;
}

float knn_result_max_dist(const KNNResult *r)
{
    float maxd = 0.0f;
    for (int i = 0; i < r->count; i++) {
        if (r->neighbors[i].distance > maxd)
            maxd = r->neighbors[i].distance;
    }
    return maxd;
}

int knn_result_has_id(const KNNResult *r, int id)
{
    for (int i = 0; i < r->count; i++) {
        if (r->neighbors[i].id == id) return 1;
    }
    return 0;
}
