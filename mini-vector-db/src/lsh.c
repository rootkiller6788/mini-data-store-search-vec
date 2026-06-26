#include "lsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float rand_gaussian(void)
{
    float u1 = (float)rand() / (float)RAND_MAX;
    float u2 = (float)rand() / (float)RAND_MAX;
    return sqrtf(-2.0f * logf(u1 + 1e-10f)) * cosf(2.0f * 3.14159265f * u2);
}

void lsh_init(LSHTable *table)
{
    table->num_vectors = 0;
    memset(table->vectors, 0, sizeof(table->vectors));
    memset(table->ids, 0, sizeof(table->ids));

    for (int t = 0; t < LSH_NUM_TABLES; t++) {
        for (int h = 0; h < LSH_NUM_HASHES; h++) {
            LSHHash *hash = &table->hashes[t][h];
            for (int d = 0; d < DIM_MAX; d++) {
                hash->random_proj[d] = rand_gaussian();
            }
            hash->bias = (float)rand() / (float)RAND_MAX * 0.1f;
        }
        for (int b = 0; b < LSH_TABLE_SIZE; b++) {
            table->buckets[t][b].bucket_size = 0;
        }
    }
}

static int compute_hash(const LSHHash *hashes, int n_hashes,
                        const Vector *vec)
{
    int hash_val = 0;
    for (int h = 0; h < n_hashes; h++) {
        float dot = 0.0f;
        for (int d = 0; d < vec->dim; d++) {
            dot += hashes[h].random_proj[d] * vec->data[d];
        }
        dot += hashes[h].bias;
        hash_val = (hash_val << 1) | (dot > 0.0f ? 1 : 0);
    }
    return hash_val & (LSH_TABLE_SIZE - 1);
}

void lsh_insert(LSHTable *table, const Vector *vec, int id)
{
    if (table->num_vectors >= LSH_MAX_VECTORS) return;

    int idx = table->num_vectors;
    table->vectors[idx] = *vec;
    table->ids[idx] = id;

    for (int t = 0; t < LSH_NUM_TABLES; t++) {
        int bucket = compute_hash(table->hashes[t], LSH_NUM_HASHES, vec);
        LSHBucket *b = &table->buckets[t][bucket];
        if (b->bucket_size < LSH_MAX_VECTORS) {
            b->bucket_ids[b->bucket_size++] = idx;
        }
    }

    table->num_vectors++;
}

static void lsh_find_candidates(const LSHTable *table, const Vector *query,
                                KNNResult *candidates)
{
    int *seen = (int *)calloc(table->num_vectors, sizeof(int));
    if (!seen) return;

    for (int t = 0; t < LSH_NUM_TABLES; t++) {
        int bucket = compute_hash(table->hashes[t], LSH_NUM_HASHES, query);
        LSHBucket *b = &table->buckets[t][bucket];

        for (int i = 0; i < b->bucket_size; i++) {
            int vidx = b->bucket_ids[i];
            if (seen[vidx]) continue;
            seen[vidx] = 1;

            float dist = vec_euclidean_dist(&table->vectors[vidx], query);
            knn_result_add(candidates, table->ids[vidx], dist);
        }
    }

    free(seen);
}

void lsh_search(const LSHTable *table, const Vector *query,
                int k, int num_probes, KNNResult *result)
{
    knn_result_init(result, k);

    KNNResult candidates;
    knn_result_init(&candidates, table->num_vectors > 5000 ? 5000 : table->num_vectors);

    lsh_find_candidates(table, query, &candidates);

    knn_result_sort(&candidates);

    for (int i = 0; i < candidates.count && i < k; i++) {
        result->neighbors[i] = candidates.neighbors[i];
        result->count++;
    }

    (void)num_probes;
}

void lsh_print_stats(const LSHTable *table)
{
    printf("=== LSH Table Statistics ===\n");
    printf("  Tables:        %d\n", LSH_NUM_TABLES);
    printf("  Hashes/table:  %d\n", LSH_NUM_HASHES);
    printf("  Vectors:       %d\n", table->num_vectors);
    printf("  Max vectors:   %d\n", LSH_MAX_VECTORS);

    int total_nonempty = 0;
    int max_bucket = 0;
    long total_entries = 0;
    for (int t = 0; t < LSH_NUM_TABLES; t++) {
        for (int b = 0; b < LSH_TABLE_SIZE; b++) {
            int sz = table->buckets[t][b].bucket_size;
            if (sz > 0) total_nonempty++;
            if (sz > max_bucket) max_bucket = sz;
            total_entries += sz;
        }
    }
    printf("  Non-empty buckets: %d\n", total_nonempty);
    printf("  Max bucket size:   %d\n", max_bucket);
    printf("  Avg bucket size:   %.2f\n",
           total_nonempty > 0 ? (float)total_entries / total_nonempty : 0.0f);
    printf("==========================\n");
}
