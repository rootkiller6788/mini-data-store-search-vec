#include "vector_math.h"
#include "exact_knn.h"
#include "hnsw.h"
#include "ivf_pq.h"
#include "lsh.h"
#include "distance_metrics.h"
#include "vector_db.h"
#include "serialization.h"
#include "index_config.h"
#include "pq_full.h"
#include "kmeans_pp.h"
#include "dimensionality.h"
#include "index_eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#define TEST_DIM 16
#define TEST_N   50
#define TEST_K   5

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s... ", #name); \
    fflush(stdout); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

/* L1/L2 Tests: Vector Math */
static void test_vector_operations(void) {
    TEST(vector_operations);
    Vector a, b, c;
    vec_zero(&a, TEST_DIM);
    vec_zero(&b, TEST_DIM);
    for (int i = 0; i < TEST_DIM; i++) {
        a.data[i] = (float)(i + 1);
        b.data[i] = (float)(TEST_DIM - i);
    }
    a.dim = TEST_DIM;
    b.dim = TEST_DIM;

    float dot = vec_dot_product(&a, &b);
    assert(dot > 0.0f);

    float l2_dist = vec_euclidean_dist(&a, &b);
    assert(l2_dist > 0.0f);

    float cos_sim = vec_cosine_similarity(&a, &b);
    assert(cos_sim >= -1.0f && cos_sim <= 1.0f);

    vec_copy(&a, &c);
    assert(c.dim == a.dim);
    assert(fabsf(c.data[0] - a.data[0]) < 1e-6f);

    vec_add(&a, &b, &c);
    assert(c.data[0] == a.data[0] + b.data[0]);

    vec_l2_normalize(&a);
    float norm = vec_l2_norm(&a);
    assert(fabsf(norm - 1.0f) < 1e-5f);

    PASS();
}

/* L2 Test: Distance metrics */
static void test_distance_metrics(void) {
    TEST(distance_metrics);
    Vector a, b;
    vec_fill_random(&a, TEST_DIM);
    vec_fill_random(&b, TEST_DIM);

    float l2 = metric_distance(&a, &b, METRIC_L2);
    float l1 = metric_distance(&a, &b, METRIC_L1);
    float sq = metric_distance(&a, &b, METRIC_SQEUCLIDEAN);
    assert(l2 > 0.0f);
    assert(l1 > 0.0f);
    assert(fabsf(sq - l2 * l2) < 1e-4f);

    assert(metric_check_identity(&a, METRIC_L2) == 1);
    assert(metric_check_symmetry(&a, &b, METRIC_L2) == 1);

    const char *name = metric_name(METRIC_L1);
    assert(name != NULL && strlen(name) > 0);

    PASS();
}

/* L2 Test: Exact KNN */
static void test_exact_knn(void) {
    TEST(exact_knn);
    Vector *dataset = (Vector *)malloc(TEST_N * sizeof(Vector));
    if (!dataset) { printf("FAIL (malloc)\n"); return; }
    srand(42);
    for (int i = 0; i < TEST_N; i++) {
        vec_fill_random(&dataset[i], TEST_DIM);
    }
    Vector query;
    vec_fill_random(&query, TEST_DIM);

    KNNResult *result = (KNNResult *)malloc(sizeof(KNNResult));
    if (!result) { printf("FAIL (malloc)\n"); free(dataset); return; }
    *result = knn_search(dataset, TEST_N, &query, TEST_K);
    int got = result->count;
    if (got != TEST_K) { printf("FAIL (count=%d expected=%d)\n", got, TEST_K); free(dataset); free(result); return; }
    for (int i = 0; i < got - 1; i++) {
        if (result->neighbors[i].distance > result->neighbors[i+1].distance + 1e-6f) {
            printf("FAIL (unsorted results)\n"); free(dataset); free(result); return;
        }
    }

    /* Test result management */
    KNNResult *r2 = (KNNResult *)malloc(sizeof(KNNResult));
    if (!r2) { printf("FAIL (malloc)\n"); free(dataset); free(result); return; }
    knn_result_init(r2, 3);
    if (r2->count != 0) { printf("FAIL (init count)\n"); free(dataset); free(result); free(r2); return; }
    knn_result_add(r2, 0, 1.0f);
    knn_result_add(r2, 1, 0.5f);
    knn_result_add(r2, 2, 2.0f);
    knn_result_sort(r2);
    if (r2->count != 3) { printf("FAIL (add count)\n"); free(dataset); free(result); free(r2); return; }
    if (r2->neighbors[0].distance > r2->neighbors[1].distance) {
        printf("FAIL (sort order)\n"); free(dataset); free(result); free(r2); return;
    }

    free(dataset);
    free(result);
    free(r2);
    PASS();
}

/* L3 Test: HNSW */
static void test_hnsw(void) {
    TEST(hnsw);
    HNSWGraph *graph = (HNSWGraph *)malloc(sizeof(HNSWGraph));
    if (!graph) { printf("FAIL (malloc)\n"); return; }
    hnsw_init(graph, 8, 50);

    Vector *vectors = (Vector *)malloc(30 * sizeof(Vector));
    if (!vectors) { printf("FAIL (malloc)\n"); free(graph); return; }
    srand(123);
    for (int i = 0; i < 30; i++) {
        vec_fill_random(&vectors[i], TEST_DIM);
        hnsw_insert(graph, &vectors[i], i);
    }
    if (graph->num_nodes != 30) { printf("FAIL (nodes %d)\n", graph->num_nodes); free(graph); free(vectors); return; }
    if (graph->entry_point < 0) { printf("FAIL (entry)\n"); free(graph); free(vectors); return; }

    Vector query;
    vec_fill_random(&query, TEST_DIM);
    KNNResult result;
    hnsw_search(graph, &query, 3, 16, &result);
    if (result.count <= 0) { printf("FAIL (empty search)\n"); free(graph); free(vectors); return; }

    KNNResult gt = knn_search(vectors, 30, &query, 3);
    float recall = knn_recall_at_k(&gt, &result, 3);
    if (recall <= 0.0f) { printf("FAIL (recall=0)\n"); free(graph); free(vectors); return; }

    free(graph);
    free(vectors);
    PASS();
}

/* L4 Test: IVF-PQ */
static void test_ivf_pq(void) {
    TEST(ivf_pq);
    IVFIndex *index = (IVFIndex *)malloc(sizeof(IVFIndex));
    if (!index) { printf("FAIL (malloc)\n"); return; }
    ivf_init(index);

    Vector *vectors = (Vector *)malloc(40 * sizeof(Vector));
    if (!vectors) { printf("FAIL (malloc)\n"); free(index); return; }
    srand(456);
    for (int i = 0; i < 40; i++) {
        vec_fill_random(&vectors[i], TEST_DIM);
    }

    ivf_train(index, vectors, 40, IVF_MAX_CENTROIDS);
    if (index->trained != 1) { printf("FAIL (train)\n"); free(index); free(vectors); return; }

    for (int i = 0; i < 40; i++) {
        ivf_add(index, &vectors[i], i);
    }
    if (index->num_vectors != 40) { printf("FAIL (add %d)\n", index->num_vectors); free(index); free(vectors); return; }

    Vector query;
    vec_fill_random(&query, TEST_DIM);
    KNNResult result;
    ivf_search(index, &query, 3, IVF_NPROBE, &result);
    if (result.count <= 0) { printf("FAIL (empty)\n"); free(index); free(vectors); return; }

    free(index);
    free(vectors);
    PASS();
}

/* L5 Test: LSH */
static void test_lsh(void) {
    TEST(lsh);
    LSHTable *table = (LSHTable *)malloc(sizeof(LSHTable));
    if (!table) { printf("FAIL (malloc)\n"); return; }
    lsh_init(table);

    Vector *vectors = (Vector *)malloc(50 * sizeof(Vector));
    if (!vectors) { printf("FAIL (malloc)\n"); free(table); return; }
    srand(789);
    for (int i = 0; i < 50; i++) {
        vec_fill_random(&vectors[i], TEST_DIM);
        lsh_insert(table, &vectors[i], i);
    }
    if (table->num_vectors != 50) { printf("FAIL (count %d)\n", table->num_vectors); free(table); free(vectors); return; }

    Vector query;
    vec_fill_random(&query, TEST_DIM);
    KNNResult result;
    lsh_search(table, &query, 3, 1, &result);
    if (result.count <= 0) { printf("FAIL (empty)\n"); free(table); free(vectors); return; }

    free(table);
    free(vectors);
    PASS();
}

/* L6 Test: Flat index insert/search/delete (VectorDB core ops without large structs) */
static void test_flat_index_ops(void) {
    TEST(flat_index_ops);
    int capacity = 20;
    Vector *vectors = (Vector *)malloc(capacity * sizeof(Vector));
    int *ids = (int *)malloc(capacity * sizeof(int));
    if (!vectors || !ids) { printf("FAIL (malloc)\n"); free(vectors); free(ids); return; }

    int n = 0;
    srand(7777);
    for (int i = 0; i < 5; i++) {
        vec_fill_random(&vectors[n], TEST_DIM);
        ids[n] = i;
        n++;
    }
    if (n != 5) { printf("FAIL (n)\n"); free(vectors); free(ids); return; }

    Vector query;
    vec_fill_random(&query, TEST_DIM);
    KNNResult result = knn_search(vectors, n, &query, 3);
    if (result.count != 3) { printf("FAIL (knn size)\n"); free(vectors); free(ids); return; }

    /* Delete by swapping with last */
    int del_id = 2;
    for (int i = 0; i < n; i++) {
        if (ids[i] == del_id) {
            vectors[i] = vectors[n-1];
            ids[i] = ids[n-1];
            n--;
            break;
        }
    }
    if (n != 4) { printf("FAIL (del size)\n"); free(vectors); free(ids); return; }

    /* Verify get */
    int found = 0;
    for (int i = 0; i < n; i++) { if (ids[i] == 3) { found = 1; break; } }
    if (!found) { printf("FAIL (get id)\n"); free(vectors); free(ids); return; }

    free(vectors);
    free(ids);
    PASS();
}

/* L7 Test: Serialization roundtrip (no VectorDB - direct struct save/load) */
static void test_serialization_roundtrip(void) {
    TEST(serialization);
    /* Test u32 serialization */
    const char *fname = "test_ser.bin";
    FILE *fp = fopen(fname, "wb");
    if (!fp) { printf("FAIL (fopen w)\n"); return; }
    ser_write_u32(fp, 0xDEADBEEF);
    ser_write_i32(fp, -42);
    ser_write_string(fp, "hello");
    fclose(fp);

    fp = fopen(fname, "rb");
    if (!fp) { printf("FAIL (fopen r)\n"); return; }
    unsigned int u32;
    int i32;
    char str[64];
    if (!ser_read_u32(fp, &u32) || u32 != 0xDEADBEEF) { printf("FAIL (u32)\n"); fclose(fp); return; }
    if (!ser_read_i32(fp, &i32) || i32 != -42) { printf("FAIL (i32)\n"); fclose(fp); return; }
    if (!ser_read_string(fp, str, 64) || strcmp(str, "hello") != 0) { printf("FAIL (str)\n"); fclose(fp); return; }
    fclose(fp);
    remove(fname);

    /* Test CRC32 */
    const unsigned char test_data[] = "123456789";
    unsigned int crc = ser_crc32(test_data, 9);
    if (crc == 0) { printf("FAIL (crc)\n"); return; }

    PASS();
}

/* L4 Test: Index configuration validation */
static void test_index_config(void) {
    TEST(index_config);
    IndexConfig cfg;
    index_config_init(&cfg, 128);

    char err[256];
    if (index_config_validate(&cfg, err, sizeof(err)) != 0) { printf("FAIL (validate)\n"); return; }

    index_config_set_strategy(&cfg, IDX_STRATEGY_ACCURACY);
    if (cfg.hnsw_M != 32) { printf("FAIL (strategy)\n"); return; }

    index_config_autotune(&cfg, 10000);
    if (cfg.ivf_nlist <= 0) { printf("FAIL (autotune)\n"); return; }

    size_t mem = index_config_estimate_memory(&cfg, 1000);
    if (mem == 0) { printf("FAIL (mem est)\n"); return; }

    IndexConfig cfg2;
    index_config_init(&cfg2, 128);
    if (index_config_equals(&cfg, &cfg2) != 0) { printf("FAIL (equals)\n"); return; }

    PASS();
}

/* L7 Test: Recall evaluation */
static void test_eval_recall(void) {
    TEST(eval_recall);
    KNNResult gt, approx;
    knn_result_init(&gt, 5);
    knn_result_init(&approx, 5);

    for (int i = 0; i < 5; i++) {
        knn_result_add(&gt, i, (float)i);
        knn_result_add(&approx, i, (float)i);
    }

    float recall = eval_recall_at_k(&gt, &approx, 5);
    if (fabsf(recall - 1.0f) >= 1e-5f) { printf("FAIL (recall=%.6f)\n", recall); return; }

    PASS();
}

/* L8 Test: k-means++ */
static void test_kmeans(void) {
    TEST(kmeans);
    Vector vectors[30];
    srand(555);
    for (int i = 0; i < 30; i++) {
        vec_fill_random(&vectors[i], TEST_DIM);
    }

    float (*centroids)[DIM_MAX] = (float (*)[DIM_MAX])calloc(3 * DIM_MAX, sizeof(float));
    int *assignments = (int *)calloc(30, sizeof(int));
    if (!centroids || !assignments) { free(centroids); free(assignments); printf("FAIL (malloc)\n"); return; }

    int iters = kmeans_cluster(vectors, 30, TEST_DIM, 3, 20, centroids, assignments);
    if (iters <= 0) { printf("FAIL (iters)\n"); free(centroids); free(assignments); return; }

    float sil = kmeans_silhouette(vectors, 30, TEST_DIM, assignments, 3);
    if (sil < -1.0f || sil > 1.0f) { printf("FAIL (sil=%.4f)\n", sil); free(centroids); free(assignments); return; }

    free(centroids);
    free(assignments);
    PASS();
}

/* L9 Test: PCA */
static void test_pca(void) {
    TEST(pca);
    Vector vectors[20];
    srand(777);
    for (int i = 0; i < 20; i++) {
        vec_fill_random(&vectors[i], TEST_DIM);
    }

    float *cov = (float *)malloc(TEST_DIM * TEST_DIM * sizeof(float));
    float (*eigvecs)[DIM_MAX] = (float (*)[DIM_MAX])calloc(3 * DIM_MAX, sizeof(float));
    float eigenvals[3];
    if (!cov || !eigvecs) { free(cov); free(eigvecs); printf("FAIL (malloc)\n"); return; }

    int ret = dim_pca(vectors, 20, 3, cov, eigvecs, eigenvals);
    if (ret != 0) { printf("FAIL (pca)\n"); free(cov); free(eigvecs); return; }
    if (eigenvals[0] < eigenvals[1]) { printf("FAIL (eig order)\n"); free(cov); free(eigvecs); return; }

    free(cov);
    free(eigvecs);
    PASS();
}

/* L5 Test: JL lemma */
static void test_jl_lemma(void) {
    TEST(jl_lemma);
    Vector vectors[10];
    srand(888);
    for (int i = 0; i < 10; i++) {
        vec_fill_random(&vectors[i], TEST_DIM);
    }

    int k = 4;
    float (*R)[DIM_MAX] = (float (*)[DIM_MAX])calloc(k * DIM_MAX, sizeof(float));
    if (!R) { printf("FAIL (malloc)\n"); return; }

    dim_random_projection_matrix(R, k, TEST_DIM);
    float pass_rate = dim_verify_jl(vectors, 10, (const float (*)[DIM_MAX])R, k, 0.5f);
    if (pass_rate <= 0.0f) { printf("FAIL (jl)\n"); free(R); return; }

    free(R);
    PASS();
}

int main(void) {
    printf("=== mini-vector-db Test Suite ===\n\n");

    srand((unsigned)time(NULL));

    test_vector_operations();
    test_distance_metrics();
    test_exact_knn();
    test_hnsw();
    test_ivf_pq();
    test_lsh();
    test_flat_index_ops();
    test_serialization_roundtrip();
    test_index_config();
    test_eval_recall();
    test_kmeans();
    test_pca();
    test_jl_lemma();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}