#include "hnsw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

static int random_level(float ml)
{
    float r = (float)rand() / (float)RAND_MAX;
    return (int)(-logf(r + 1e-10f) * ml);
}

void hnsw_init(HNSWGraph *graph, int M, int ef_construction)
{
    graph->entry_point = -1;
    graph->num_nodes   = 0;
    graph->M           = M;
    graph->Mmax0       = 2 * M;
    graph->ef_construction = ef_construction;
    memset(graph->nodes, 0, sizeof(graph->nodes));
}

static void select_neighbors_heuristic(HNSWGraph *graph,
                                       const Vector *query,
                                       KNNResult *candidates,
                                       int M, int level)
{
    knn_result_prune(candidates, M);
}

static void hnsw_search_layer(const HNSWGraph *graph,
                              const Vector *query,
                              KNNResult *entry_points,
                              int ef, int lc)
{
    KNNResult visited;
    knn_result_init(&visited, ef * 10);

    for (int i = 0; i < entry_points->count; i++) {
        int ep = entry_points->neighbors[i].id;
        float d = vec_euclidean_dist(&graph->nodes[ep].vector, query);
        knn_result_add(&visited, ep, d);
    }

    KNNResult candidates;
    knn_result_init(&candidates, ef * 10);
    for (int i = 0; i < visited.count; i++) {
        knn_result_add(&candidates,
                       visited.neighbors[i].id,
                       visited.neighbors[i].distance);
    }
    knn_result_sort(&candidates);

    int front = 0;
    while (front < candidates.count) {
        int cid = candidates.neighbors[front].id;
        float cd = candidates.neighbors[front].distance;
        front++;

        float worst = candidates.count > 0
            ? candidates.neighbors[candidates.count - 1].distance
            : FLT_MAX;
        if (cd > worst && candidates.count >= ef) break;

        for (int i = 0; i < graph->nodes[cid].n_neighbors[lc]; i++) {
            int nid = graph->nodes[cid].neighbors[lc][i];
            if (knn_result_has_id(&visited, nid)) continue;
            knn_result_add(&visited, nid, 0.0f);

            float d = vec_euclidean_dist(&graph->nodes[nid].vector, query);
            float worst_dist = candidates.count > 0
                ? candidates.neighbors[candidates.count - 1].distance
                : FLT_MAX;
            if (d < worst_dist || candidates.count < ef) {
                knn_result_add(&candidates, nid, d);
                knn_result_sort(&candidates);
                if (candidates.count > ef) {
                    candidates.count = ef;
                }
            }
        }
    }

    knn_result_sort(&candidates);
    *entry_points = candidates;
}

void hnsw_insert(HNSWGraph *graph, const Vector *vec, int id)
{
    int level = random_level(1.0f / logf((float)graph->M));
    if (level >= graph->M) level = graph->M - 1;
    if (level >= HNSW_MAX_LEVEL) level = HNSW_MAX_LEVEL - 1;

    if (graph->entry_point == -1) {
        HNSWNode *node = &graph->nodes[graph->num_nodes];
        node->id = id;
        node->level = level;
        node->vector = *vec;
        for (int l = 0; l < HNSW_MAX_LEVEL; l++) {
            node->n_neighbors[l] = 0;
        }
        graph->entry_point = graph->num_nodes;
        graph->num_nodes++;
        return;
    }

    int ep = graph->entry_point;
    int top_level = graph->nodes[ep].level;
    KNNResult curr_eps;
    knn_result_init(&curr_eps, 1);
    knn_result_add(&curr_eps, ep,
                   vec_euclidean_dist(&graph->nodes[ep].vector, vec));

    for (int lc = top_level; lc > level; lc--) {
        hnsw_search_layer(graph, vec, &curr_eps, 1, lc);
        if (curr_eps.count > 0) {
            ep = curr_eps.neighbors[0].id;
            knn_result_init(&curr_eps, 1);
            knn_result_add(&curr_eps, ep,
                vec_euclidean_dist(&graph->nodes[ep].vector, vec));
        }
    }

    int node_idx = graph->num_nodes;
    HNSWNode *node = &graph->nodes[node_idx];
    node->id = id;
    node->level = level;
    node->vector = *vec;
    for (int l = 0; l < HNSW_MAX_LEVEL; l++) {
        node->n_neighbors[l] = 0;
    }
    graph->num_nodes++;

    for (int lc = level; lc >= 0; lc--) {
        hnsw_search_layer(graph, vec, &curr_eps, graph->ef_construction, lc);
        select_neighbors_heuristic(graph, vec, &curr_eps, graph->M, lc);

        int Mmax = (lc == 0) ? graph->Mmax0 : graph->M;
        int n_conn = curr_eps.count < Mmax ? curr_eps.count : Mmax;

        for (int i = 0; i < n_conn; i++) {
            int nid = curr_eps.neighbors[i].id;
            HNSWNode *neighbor = &graph->nodes[nid];
            if (neighbor->n_neighbors[lc] < Mmax) {
                neighbor->neighbors[lc][neighbor->n_neighbors[lc]++] = node_idx;
            }
            node->neighbors[lc][node->n_neighbors[lc]++] = nid;
        }
    }

    if (level > graph->nodes[graph->entry_point].level) {
        graph->entry_point = node_idx;
    }
}

void hnsw_search(const HNSWGraph *graph, const Vector *query,
                 int k, int ef, KNNResult *result)
{
    if (graph->entry_point == -1) {
        knn_result_init(result, k);
        return;
    }

    int ep = graph->entry_point;
    int top_level = graph->nodes[ep].level;
    KNNResult eps;
    knn_result_init(&eps, 1);
    knn_result_add(&eps, ep,
                   vec_euclidean_dist(&graph->nodes[ep].vector, query));

    for (int lc = top_level; lc > 0; lc--) {
        hnsw_search_layer(graph, query, &eps, 1, lc);
        if (eps.count > 0) {
            ep = eps.neighbors[0].id;
            knn_result_init(&eps, 1);
            knn_result_add(&eps, ep,
                vec_euclidean_dist(&graph->nodes[ep].vector, query));
        }
    }

    int ef_search = ef > k ? ef : k * 2;
    if (ef_search < k) ef_search = k;
    eps.count = 1;
    hnsw_search_layer(graph, query, &eps, ef_search, 0);

    knn_result_sort(&eps);
    knn_result_init(result, k);
    for (int i = 0; i < eps.count && i < k; i++) {
        result->neighbors[i] = eps.neighbors[i];
        result->count++;
    }
}

void hnsw_print_stats(const HNSWGraph *graph)
{
    printf("=== HNSW Graph Statistics ===\n");
    printf("  Nodes:         %d\n", graph->num_nodes);
    printf("  Entry point:   %d\n", graph->entry_point);
    printf("  M:             %d\n", graph->M);
    printf("  Mmax0:         %d\n", graph->Mmax0);
    printf("  ef_construct:  %d\n", graph->ef_construction);

    int total_edges = 0;
    int max_level = 0;
    for (int i = 0; i < graph->num_nodes; i++) {
        if (graph->nodes[i].level > max_level)
            max_level = graph->nodes[i].level;
        for (int l = 0; l <= graph->nodes[i].level; l++) {
            total_edges += graph->nodes[i].n_neighbors[l];
        }
    }
    printf("  Total edges:   %d\n", total_edges);
    printf("  Max level:     %d\n", max_level);
    printf("  Avg edges/node: %.2f\n",
           graph->num_nodes > 0
               ? (float)total_edges / graph->num_nodes : 0.0f);
    printf("=============================\n");
}
