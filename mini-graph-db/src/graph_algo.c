#include "graph_algo.h"
#include "graph_traversal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static int compare_ranked(const void *a, const void *b) {
    double diff = ((RankedNode *)b)->score - ((RankedNode *)a)->score;
    return (diff > 0) ? 1 : ((diff < 0) ? -1 : 0);
}

int pagerank(PropertyGraph *g, RankedNode *results, int max_results,
             double damping, int max_iter, double epsilon) {
    int n = graph_node_count(g);
    if (n == 0 || max_results <= 0) return 0;

    double *pr = calloc((size_t)n, sizeof(double));
    double *pr_new = calloc((size_t)n, sizeof(double));
    int *out_deg = calloc((size_t)n, sizeof(int));
    if (!pr || !pr_new || !out_deg) {
        free(pr); free(pr_new); free(out_deg);
        return 0;
    }

    for (int i = 0; i < n; i++)
        out_deg[i] = node_out_degree(g, g->nodes[i].id);

    double init = 1.0 / (double)n;
    for (int i = 0; i < n; i++) pr[i] = init;

    for (int iter = 0; iter < max_iter; iter++) {
        double dangling_sum = 0.0;
        for (int i = 0; i < n; i++) {
            if (out_deg[i] == 0)
                dangling_sum += pr[i];
        }
        dangling_sum /= (double)n;

        for (int i = 0; i < n; i++) {
            pr_new[i] = (1.0 - damping) / (double)n + damping * dangling_sum;
        }

        for (int i = 0; i < n; i++) {
            if (out_deg[i] > 0) {
                double share = pr[i] / (double)out_deg[i];
                AdjListNode *adj = g->adjacency[i].head;
                while (adj) {
                    int ni = 0;
                    for (int j = 0; j < n; j++) {
                        if (g->nodes[j].id == adj->neighbor_id) { ni = j; break; }
                    }
                    pr_new[ni] += damping * share;
                    adj = adj->next;
                }
            }
        }

        double delta = 0.0;
        for (int i = 0; i < n; i++) {
            delta += fabs(pr_new[i] - pr[i]);
            pr[i] = pr_new[i];
        }
        if (delta < epsilon) break;
    }

    int count = (n < max_results) ? n : max_results;
    for (int i = 0; i < count; i++) {
        results[i].node_id = g->nodes[i].id;
        results[i].score = pr[i];
    }
    qsort(results, (size_t)n, sizeof(RankedNode), compare_ranked);

    free(pr); free(pr_new); free(out_deg);
    return count;
}

int pagerank_print_top(PropertyGraph *g, int top_n) {
    int n = graph_node_count(g);
    RankedNode *results = calloc((size_t)n, sizeof(RankedNode));
    if (!results) return 0;
    int count = pagerank(g, results, top_n, PAGERANK_DAMPING,
                         PAGERANK_MAX_ITER, PAGERANK_EPSILON);
    printf("\n=== PageRank (top %d) ===\n", top_n);
    for (int i = 0; i < count; i++)
        printf("  #%d: node=%lld  score=%.6f\n",
               i + 1, (long long)results[i].node_id, results[i].score);
    free(results);
    return count;
}

int label_propagation(PropertyGraph *g, LabelAssignment *results,
                      int max_results, int max_iter) {
    int n = graph_node_count(g);
    if (n == 0) return 0;

    int *labels = calloc((size_t)n, sizeof(int));
    if (!labels) return 0;
    for (int i = 0; i < n; i++) labels[i] = i;

    for (int iter = 0; iter < max_iter; iter++) {
        bool changed = false;
        for (int i = 0; i < n; i++) {
            int neighbor_labels[MAX_NODES];
            int nl_count = 0;
            AdjListNode *adj = g->adjacency[i].head;
            while (adj) {
                int ni = 0;
                for (int j = 0; j < n; j++) {
                    if (g->nodes[j].id == adj->neighbor_id) { ni = j; break; }
                }
                if (nl_count < MAX_NODES)
                    neighbor_labels[nl_count++] = labels[ni];
                adj = adj->next;
            }
            if (nl_count == 0) continue;

            int freq[MAX_NODES] = {0};
            for (int k = 0; k < nl_count; k++)
                freq[neighbor_labels[k]]++;

            int best_label = labels[i];
            int best_freq = 0;
            for (int k = 0; k < nl_count; k++) {
                int lbl = neighbor_labels[k];
                if (freq[lbl] > best_freq ||
                    (freq[lbl] == best_freq && lbl < best_label)) {
                    best_freq = freq[lbl];
                    best_label = lbl;
                }
            }
            if (best_label != labels[i]) {
                labels[i] = best_label;
                changed = true;
            }
        }
        if (!changed) break;
    }

    int count = (n < max_results) ? n : max_results;
    for (int i = 0; i < count; i++) {
        results[i].node_id = g->nodes[i].id;
        results[i].label = labels[i];
    }
    free(labels);
    return count;
}

int label_propagation_print(PropertyGraph *g) {
    int n = graph_node_count(g);
    LabelAssignment *results = calloc((size_t)n, sizeof(LabelAssignment));
    if (!results) return 0;
    int count = label_propagation(g, results, n, 50);
    printf("\n=== Label Propagation (Community Detection) ===\n");
    for (int i = 0; i < count; i++)
        printf("  node=%lld  community=%d\n",
               (long long)results[i].node_id, results[i].label);
    free(results);
    return count;
}

static int uf_find(int *parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

static void uf_union(int *parent, int *rank, int x, int y) {
    int rx = uf_find(parent, x);
    int ry = uf_find(parent, y);
    if (rx == ry) return;
    if (rank[rx] < rank[ry]) {
        parent[rx] = ry;
    } else if (rank[rx] > rank[ry]) {
        parent[ry] = rx;
    } else {
        parent[ry] = rx;
        rank[rx]++;
    }
}

int connected_components(PropertyGraph *g, int64_t *component_of,
                         int *component_count) {
    int n = graph_node_count(g);
    int *parent = calloc((size_t)n, sizeof(int));
    int *rank = calloc((size_t)n, sizeof(int));
    if (!parent || !rank) { free(parent); free(rank); return 0; }

    for (int i = 0; i < n; i++) parent[i] = i;

    for (int i = 0; i < g->edge_count; i++) {
        int from_idx = -1, to_idx = -1;
        for (int j = 0; j < n; j++) {
            if (g->nodes[j].id == g->edges[i].from_node) from_idx = j;
            if (g->nodes[j].id == g->edges[i].to_node) to_idx = j;
        }
        if (from_idx >= 0 && to_idx >= 0)
            uf_union(parent, rank, from_idx, to_idx);
    }

    int comp_root[MAX_NODES];
    int comp_map[MAX_NODES];
    memset(comp_map, -1, sizeof(comp_map));
    int comp_count = 0;

    for (int i = 0; i < n; i++) {
        int r = uf_find(parent, i);
        int assigned = -1;
        for (int c = 0; c < comp_count; c++) {
            if (comp_root[c] == r) { assigned = c; break; }
        }
        if (assigned < 0) {
            comp_root[comp_count] = r;
            assigned = comp_count;
            comp_count++;
        }
        component_of[i] = assigned;
    }
    *component_count = comp_count;

    free(parent); free(rank);
    return n;
}

int connected_components_print(PropertyGraph *g) {
    int n = graph_node_count(g);
    int64_t *comp = calloc((size_t)n, sizeof(int64_t));
    if (!comp) return 0;
    int comp_count = 0;
    connected_components(g, comp, &comp_count);
    printf("\n=== Connected Components: %d ===\n", comp_count);
    for (int c = 0; c < comp_count; c++) {
        printf("  Component %d: ", c);
        for (int i = 0; i < n; i++) {
            if (comp[i] == c) printf("%lld ", (long long)g->nodes[i].id);
        }
        printf("\n");
    }
    free(comp);
    return comp_count;
}

bool topological_sort(PropertyGraph *g, int64_t *sorted, int *count) {
    int n = graph_node_count(g);
    int *in_deg = calloc((size_t)n, sizeof(int));
    if (!in_deg) return false;

    for (int i = 0; i < n; i++)
        in_deg[i] = node_in_degree(g, g->nodes[i].id);

    int queue[MAX_NODES];
    int front = 0, rear = 0;
    for (int i = 0; i < n; i++) {
        if (in_deg[i] == 0) queue[rear++] = i;
    }

    *count = 0;
    while (front < rear) {
        int ci = queue[front++];
        sorted[(*count)++] = g->nodes[ci].id;

        AdjListNode *adj = g->adjacency[ci].head;
        while (adj) {
            int ni = 0;
            for (int j = 0; j < n; j++) {
                if (g->nodes[j].id == adj->neighbor_id) { ni = j; break; }
            }
            in_deg[ni]--;
            if (in_deg[ni] == 0) queue[rear++] = ni;
            adj = adj->next;
        }
    }
    free(in_deg);
    return (*count == n);
}

bool topological_sort_print(PropertyGraph *g) {
    int n = graph_node_count(g);
    int64_t *sorted = calloc((size_t)n, sizeof(int64_t));
    if (!sorted) return false;
    int count = 0;
    bool ok = topological_sort(g, sorted, &count);
    printf("\n=== Topological Sort ===\n");
    if (!ok) {
        printf("  Graph has a cycle - topological sort not possible\n");
        free(sorted);
        return false;
    }
    for (int i = 0; i < count; i++)
        printf("  %d: %lld\n", i + 1, (long long)sorted[i]);
    free(sorted);
    return true;
}

static bool dfs_cycle(PropertyGraph *g, int idx, int *state,
                      int *parent, int64_t *cycle, int *cycle_len) {
    state[idx] = 1;
    AdjListNode *adj = g->adjacency[idx].head;
    while (adj) {
        int ni = 0;
        for (int j = 0; j < graph_node_count(g); j++) {
            if (g->nodes[j].id == adj->neighbor_id) { ni = j; break; }
        }
        if (state[ni] == 1) {
            int cur = idx;
            int len = 0;
            while (cur != ni && len < MAX_PATH_LENGTH) {
                cycle[len++] = g->nodes[cur].id;
                cur = parent[cur];
            }
            cycle[len++] = g->nodes[ni].id;
            *cycle_len = len;
            return true;
        }
        if (state[ni] == 0) {
            parent[ni] = idx;
            if (dfs_cycle(g, ni, state, parent, cycle, cycle_len))
                return true;
        }
        adj = adj->next;
    }
    state[idx] = 2;
    return false;
}

bool cycle_detection(PropertyGraph *g, int64_t *cycle, int *cycle_len) {
    int n = graph_node_count(g);
    int *state = calloc((size_t)n, sizeof(int));
    int *parent = calloc((size_t)n, sizeof(int));
    if (!state || !parent) { free(state); free(parent); return false; }

    for (int i = 0; i < n; i++) parent[i] = -1;

    bool found = false;
    for (int i = 0; i < n && !found; i++) {
        if (state[i] == 0)
            found = dfs_cycle(g, i, state, parent, cycle, cycle_len);
    }
    free(state); free(parent);
    return found;
}

bool cycle_detection_print(PropertyGraph *g) {
    int64_t cycle[MAX_PATH_LENGTH];
    int cycle_len = 0;
    bool found = cycle_detection(g, cycle, &cycle_len);
    printf("\n=== Cycle Detection ===\n");
    if (!found) {
        printf("  No cycle detected\n");
        return false;
    }
    printf("  Cycle: ");
    for (int i = cycle_len - 1; i >= 0; i--)
        printf("%lld ", (long long)cycle[i]);
    printf("\n");
    return true;
}

int node_out_degree(PropertyGraph *g, int64_t node_id) {
    int idx = 0;
    int n = graph_node_count(g);
    for (int i = 0; i < n; i++) {
        if (g->nodes[i].id == node_id) { idx = i; break; }
    }
    int deg = 0;
    AdjListNode *adj = g->adjacency[idx].head;
    while (adj) { deg++; adj = adj->next; }
    return deg;
}

int node_in_degree(PropertyGraph *g, int64_t node_id) {
    int deg = 0;
    for (int i = 0; i < g->edge_count; i++) {
        if (g->edges[i].to_node == node_id) deg++;
    }
    return deg;
}

/* =========================================================================
 * Kruskal's Algorithm — Minimum Spanning Tree
 *
 * L5: Greedy Algorithm. Sorts edges by weight, adds them in order
 * if they don't create a cycle (checked via Union-Find with path compression).
 * Time: O(E log E) with sorting. Space: O(V) for Union-Find.
 *
 * Correctness Theorem (Kruskal, 1956):
 *   For weighted connected graph G, the greedy algorithm that adds
 *   edges in non-decreasing weight order, skipping those that create
 *   a cycle, produces a Minimum Spanning Tree.
 *
 * Proof sketch (Exchange Argument):
 *   Let T be the greedy tree, T* an optimal MST. If T != T*, there
 *   exists a first edge e in T not in T*. Adding e to T* creates a
 *   cycle. Some edge e' on that cycle is >= e by the greedy ordering.
 *   Exchanging e for e' does not increase total weight.
 *   Iterating yields T = T*.
 * ========================================================================= */

static int kruskal_uf_find(int *parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; /* path compression */
        x = parent[x];
    }
    return x;
}

static void kruskal_uf_union(int *parent, int *rank, int x, int y) {
    int rx = kruskal_uf_find(parent, x);
    int ry = kruskal_uf_find(parent, y);
    if (rx == ry) return;
    if (rank[rx] < rank[ry]) {
        parent[rx] = ry;
    } else if (rank[rx] > rank[ry]) {
        parent[ry] = rx;
    } else {
        parent[ry] = rx;
        rank[rx]++;
    }
}

int mst_kruskal(PropertyGraph *g, double (*weight_func)(Edge *e),
                MstEdge *mst, int max_edges) {
    int n = graph_node_count(g);
    int ecount = graph_edge_count(g);
    if (n == 0 || ecount == 0) return 0;

    /* collect edges with weights */
    typedef struct { int idx; double w; } EdgeWeight;
    EdgeWeight *ew = calloc((size_t)ecount, sizeof(EdgeWeight));
    if (!ew) return 0;

    for (int i = 0; i < ecount; i++) {
        ew[i].idx = i;
        ew[i].w = weight_func ? weight_func(&g->edges[i]) : 1.0;
    }

    /* sort edges by weight ascending */
    for (int i = 0; i < ecount - 1; i++) {
        for (int j = i + 1; j < ecount; j++) {
            if (ew[j].w < ew[i].w) {
                EdgeWeight tmp = ew[i]; ew[i] = ew[j]; ew[j] = tmp;
            }
        }
    }

    /* Union-Find on node indices */
    int *parent = calloc((size_t)n, sizeof(int));
    int *rank = calloc((size_t)n, sizeof(int));
    if (!parent || !rank) { free(ew); free(parent); free(rank); return 0; }
    for (int i = 0; i < n; i++) parent[i] = i;

    int mst_count = 0;
    for (int i = 0; i < ecount && mst_count < max_edges && mst_count < n - 1; i++) {
        Edge *e = &g->edges[ew[i].idx];
        int fi = 0, ti = 0;
        for (int j = 0; j < n; j++) {
            if (g->nodes[j].id == e->from_node) fi = j;
            if (g->nodes[j].id == e->to_node) ti = j;
        }
        if (kruskal_uf_find(parent, fi) != kruskal_uf_find(parent, ti)) {
            kruskal_uf_union(parent, rank, fi, ti);
            mst[mst_count].from = e->from_node;
            mst[mst_count].to = e->to_node;
            mst[mst_count].weight = ew[i].w;
            mst_count++;
        }
    }
    free(ew); free(parent); free(rank);
    return mst_count;
}

double mst_kruskal_total_weight(MstEdge *mst, int count) {
    double total = 0.0;
    for (int i = 0; i < count; i++) total += mst[i].weight;
    return total;
}

void mst_print(MstEdge *mst, int count) {
    printf("\n=== Minimum Spanning Tree: %d edges ===\n", count);
    for (int i = 0; i < count; i++)
        printf("  %lld --[%.4f]--> %lld\n",
               (long long)mst[i].from, mst[i].weight, (long long)mst[i].to);
    printf("  Total weight: %.4f\n", mst_kruskal_total_weight(mst, count));
}

/* =========================================================================
 * Prim's Algorithm — Minimum Spanning Tree
 *
 * L5: Greedy algorithm that grows a single tree.
 * Uses a priority queue (array-based min-extract) for simplicity.
 * O(V^2) with array, O(E log V) with binary heap.
 *
 * Algorithm:
 *   1. Start from arbitrary node (index 0).
 *   2. Maintain key[v] = minimum edge weight from tree to v.
 *   3. At each step, add the vertex with minimum key to the tree,
 *      and update keys of its neighbors.
 * ========================================================================= */

int mst_prim(PropertyGraph *g, double (*weight_func)(Edge *e),
             MstEdge *mst, int max_edges) {
    int n = graph_node_count(g);
    if (n == 0) return 0;

    double *key = calloc((size_t)n, sizeof(double));
    int *parent = calloc((size_t)n, sizeof(int));
    int *parent_edge = calloc((size_t)n, sizeof(int));
    bool *in_mst = calloc((size_t)n, sizeof(bool));
    if (!key || !parent || !parent_edge || !in_mst) {
        free(key); free(parent); free(parent_edge); free(in_mst);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        key[i] = INFINITY;
        parent[i] = -1;
        parent_edge[i] = -1;
    }
    key[0] = 0.0;

    int mst_count = 0;
    for (int round = 0; round < n; round++) {
        /* extract minimum key vertex not yet in MST */
        int u = -1;
        double min_key = INFINITY;
        for (int i = 0; i < n; i++) {
            if (!in_mst[i] && key[i] < min_key) {
                min_key = key[i];
                u = i;
            }
        }
        if (u < 0) break;
        in_mst[u] = true;

        if (parent[u] >= 0 && mst_count < max_edges) {
            mst[mst_count].from = g->nodes[parent[u]].id;
            mst[mst_count].to = g->nodes[u].id;
            mst[mst_count].weight = key[u];
            mst_count++;
        }

        /* update keys of neighbors */
        AdjListNode *adj = g->adjacency[u].head;
        while (adj) {
            int v = 0;
            for (int i = 0; i < n; i++) {
                if (g->nodes[i].id == adj->neighbor_id) { v = i; break; }
            }
            if (!in_mst[v]) {
                Edge *edge = graph_get_edge(g, adj->edge_id);
                double w = weight_func ? (edge ? weight_func(edge) : 1.0) : 1.0;
                if (w < key[v]) {
                    key[v] = w;
                    parent[v] = u;
                }
            }
            adj = adj->next;
        }
    }
    free(key); free(parent); free(parent_edge); free(in_mst);
    return mst_count;
}

/* =========================================================================
 * A* Search — Informed Shortest Path
 *
 * L5: Heuristic Search. f(n) = g(n) + h(n)
 *
 * Correctness: A* is optimal if heuristic h is admissible (never
 * overestimates true distance) and consistent (h(n) <= c(n,n') + h(n')).
 *
 * Implementation: priority queue via array-based extract-min.
 * ========================================================================= */

int astar_search(PropertyGraph *g, int64_t start, int64_t target,
                 double (*weight_func)(Edge *e),
                 double (*heuristic)(int64_t node, int64_t target),
                 int64_t *path, int max_path) {
    int n = graph_node_count(g);
    if (n == 0 || !path) return 0;

    double *g_score = calloc((size_t)n, sizeof(double));
    double *f_score = calloc((size_t)n, sizeof(double));
    int *came_from = calloc((size_t)n, sizeof(int));
    bool *closed = calloc((size_t)n, sizeof(bool));
    if (!g_score || !f_score || !came_from || !closed) {
        free(g_score); free(f_score); free(came_from); free(closed);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        g_score[i] = INFINITY;
        f_score[i] = INFINITY;
        came_from[i] = -1;
    }

    int si = 0;
    for (int i = 0; i < n; i++)
        if (g->nodes[i].id == start) { si = i; break; }
    g_score[si] = 0.0;
    f_score[si] = heuristic ? heuristic(start, target) : 0.0;

    for (int iter = 0; iter < n; iter++) {
        /* find open node with minimum f_score */
        int cur = -1;
        double min_f = INFINITY;
        for (int i = 0; i < n; i++) {
            if (!closed[i] && f_score[i] < min_f) {
                min_f = f_score[i];
                cur = i;
            }
        }
        if (cur < 0) break;
        if (g->nodes[cur].id == target) break;
        closed[cur] = true;

        AdjListNode *adj = g->adjacency[cur].head;
        while (adj) {
            int nb = 0;
            for (int i = 0; i < n; i++)
                if (g->nodes[i].id == adj->neighbor_id) { nb = i; break; }
            if (closed[nb]) { adj = adj->next; continue; }

            Edge *edge = graph_get_edge(g, adj->edge_id);
            double w = weight_func ? (edge ? weight_func(edge) : 1.0) : 1.0;
            double tentative_g = g_score[cur] + w;
            if (tentative_g < g_score[nb]) {
                came_from[nb] = cur;
                g_score[nb] = tentative_g;
                double h = heuristic ? heuristic(g->nodes[nb].id, target) : 0.0;
                f_score[nb] = tentative_g + h;
            }
            adj = adj->next;
        }
    }

    int ti = 0;
    for (int i = 0; i < n; i++)
        if (g->nodes[i].id == target) { ti = i; break; }

    if (came_from[ti] < 0 && si != ti) {
        free(g_score); free(f_score); free(came_from); free(closed);
        return 0;
    }

    /* reconstruct path */
    int path_len = 0;
    int cur = ti;
    int64_t rev_path[MAX_NODES];
    while (cur != si && path_len < max_path) {
        rev_path[path_len++] = g->nodes[cur].id;
        cur = came_from[cur];
    }
    if (path_len >= max_path) { path_len = 0; }
    else {
        rev_path[path_len++] = g->nodes[si].id;
        for (int i = 0; i < path_len && i < max_path; i++)
            path[i] = rev_path[path_len - 1 - i];
    }

    free(g_score); free(f_score); free(came_from); free(closed);
    return path_len;
}

/* =========================================================================
 * Edmonds-Karp Maximum Flow Algorithm
 *
 * L5: Ford-Fulkerson with BFS for shortest augmenting path.
 * Time: O(V * E^2). Space: O(V^2) for residual capacity matrix.
 *
 * Max-Flow Min-Cut Theorem (Ford-Fulkerson, 1956):
 *   The value of the maximum flow equals the capacity of the minimum cut.
 *
 * Implementation uses an adjacency matrix for residual capacities
 * because the graph may need residual edges in both directions.
 * ========================================================================= */

typedef struct {
    double capacity;
    int64_t edge_id;
} FlowEdge;

MaxFlowResult *max_flow_edmonds_karp(PropertyGraph *g, int64_t source,
                                      int64_t sink,
                                      double (*capacity_func)(Edge *e)) {
    int n = graph_node_count(g);
    if (n == 0 || n > MAX_FLOW_NODES) return NULL;

    MaxFlowResult *r = calloc(1, sizeof(MaxFlowResult));
    if (!r) return NULL;

    /* build residual capacity matrix (n x n) */
    double **residual = calloc((size_t)n, sizeof(double *));
    int **edge_map = calloc((size_t)n, sizeof(int *));
    for (int i = 0; i < n; i++) {
        residual[i] = calloc((size_t)n, sizeof(double));
        edge_map[i] = calloc((size_t)n, sizeof(int));
    }
    if (!residual || !edge_map) {
        for (int i = 0; i < n; i++) { free(residual[i]); free(edge_map[i]); }
        free(residual); free(edge_map); free(r);
        return NULL;
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            edge_map[i][j] = -1;

    /* initialize capacities from graph edges */
    for (int i = 0; i < g->edge_count; i++) {
        Edge *e = &g->edges[i];
        int fi = 0, ti = 0;
        for (int j = 0; j < n; j++) {
            if (g->nodes[j].id == e->from_node) fi = j;
            if (g->nodes[j].id == e->to_node) ti = j;
        }
        double cap = capacity_func ? capacity_func(e) : 1.0;
        residual[fi][ti] = cap;
        edge_map[fi][ti] = i;
    }

    int si = 0, sock = 0;
    for (int i = 0; i < n; i++) {
        if (g->nodes[i].id == source) si = i;
        if (g->nodes[i].id == sink) sock = i;
    }

    double max_flow = 0.0;
    int parent[MAX_FLOW_NODES];

    while (1) {
        /* BFS to find augmenting path */
        for (int i = 0; i < n; i++) parent[i] = -1;
        int queue[MAX_FLOW_NODES];
        int front = 0, rear = 0;
        queue[rear++] = si;
        parent[si] = si;

        bool found = false;
        while (front < rear && !found) {
            int u = queue[front++];
            for (int v = 0; v < n; v++) {
                if (parent[v] < 0 && residual[u][v] > 0) {
                    parent[v] = u;
                    if (v == sock) { found = true; break; }
                    queue[rear++] = v;
                }
            }
        }
        if (!found) break;

        /* find minimum residual capacity on path */
        double path_flow = INFINITY;
        for (int v = sock; v != si; v = parent[v]) {
            int u = parent[v];
            if (residual[u][v] < path_flow) path_flow = residual[u][v];
        }

        /* update residuals */
        for (int v = sock; v != si; v = parent[v]) {
            int u = parent[v];
            residual[u][v] -= path_flow;
            residual[v][u] += path_flow;
        }
        max_flow += path_flow;
    }

    r->flow_value = max_flow;
    r->path = NULL; /* simplified: don't need individual paths */
    r->path_len = 0;

    for (int i = 0; i < n; i++) {
        free(residual[i]); free(edge_map[i]);
    }
    free(residual); free(edge_map);
    return r;
}

void max_flow_free(MaxFlowResult *r) {
    if (!r) return;
    free(r->path);
    free(r);
}

/* =========================================================================
 * Kosaraju's Algorithm — Strongly Connected Components
 *
 * L5: Two-pass DFS. O(V + E) time.
 *
 * Algorithm:
 *   1. DFS on original graph, record finish times (post-order).
 *   2. Reverse graph: flip all edge directions.
 *   3. DFS on reversed graph in decreasing finish-time order.
 *      Each DFS tree is a SCC.
 *
 * Correctness (Kosaraju, 1978):
 *   The finishing times from step 1 order nodes topologically
 *   within the condensation DAG. Processing in reverse on the
 *   reversed graph ensures each DFS explores exactly one SCC.
 * ========================================================================= */

static void kosaraju_dfs1(PropertyGraph *g, int v, bool *visited,
                          int *finish_stack, int *stack_top) {
    visited[v] = true;
    AdjListNode *adj = g->adjacency[v].head;
    while (adj) {
        int w = 0;
        for (int i = 0; i < graph_node_count(g); i++)
            if (g->nodes[i].id == adj->neighbor_id) { w = i; break; }
        if (!visited[w]) kosaraju_dfs1(g, w, visited, finish_stack, stack_top);
        adj = adj->next;
    }
    finish_stack[(*stack_top)++] = v;
}

static void kosaraju_dfs2(PropertyGraph *g, int v, bool *visited,
                          int64_t *component_of, int scc_id) {
    visited[v] = true;
    component_of[v] = (int64_t)scc_id;
    /* traverse reversed graph: find all nodes that point to v */
    int n = graph_node_count(g);
    for (int i = 0; i < n; i++) {
        AdjListNode *adj = g->adjacency[i].head;
        while (adj) {
            if (adj->neighbor_id == g->nodes[v].id && !visited[i])
                kosaraju_dfs2(g, i, visited, component_of, scc_id);
            adj = adj->next;
        }
    }
}

int scc_kosaraju(PropertyGraph *g, int64_t *component_of, int *scc_count) {
    int n = graph_node_count(g);
    if (n == 0) { *scc_count = 0; return 0; }

    bool *visited = calloc((size_t)n, sizeof(bool));
    int *finish_stack = calloc((size_t)n, sizeof(int));
    if (!visited || !finish_stack) {
        free(visited); free(finish_stack);
        return 0;
    }

    int stack_top = 0;
    for (int i = 0; i < n; i++)
        if (!visited[i])
            kosaraju_dfs1(g, i, visited, finish_stack, &stack_top);

    memset(visited, 0, (size_t)n * sizeof(bool));
    int comp = 0;
    for (int i = stack_top - 1; i >= 0; i--) {
        int v = finish_stack[i];
        if (!visited[v]) {
            kosaraju_dfs2(g, v, visited, component_of, comp);
            comp++;
        }
    }
    *scc_count = comp;
    free(visited); free(finish_stack);
    return n;
}

int scc_kosaraju_print(PropertyGraph *g) {
    int n = graph_node_count(g);
    int64_t *comp = calloc((size_t)n, sizeof(int64_t));
    if (!comp) return 0;
    int scc_count = 0;
    scc_kosaraju(g, comp, &scc_count);
    printf("\n=== Strongly Connected Components: %d ===\n", scc_count);
    for (int c = 0; c < scc_count; c++) {
        printf("  SCC %d: ", c);
        for (int i = 0; i < n; i++)
            if (comp[i] == c) printf("%lld ", (long long)g->nodes[i].id);
        printf("\n");
    }
    free(comp);
    return scc_count;
}

/* =========================================================================
 * Floyd-Warshall — All-Pairs Shortest Paths
 *
 * L5: Dynamic Programming. O(V^3) time, O(V^2) space.
 *
 * Recurrence: d[i][j]^{(k)} = min(d[i][j]^{(k-1)},
 *                                  d[i][k]^{(k-1)} + d[k][j]^{(k-1)})
 *
 * where d[i][j]^{(k)} = shortest path from i to j using only
 * intermediate vertices in {0, 1, ..., k}.
 *
 * Works with negative weights but no negative cycles.
 * ========================================================================= */

int floyd_warshall(PropertyGraph *g, double *dist_matrix, int max_nodes) {
    int n = graph_node_count(g);
    if (n == 0 || max_nodes < n * n) return -1;

    for (int i = 0; i < n * n; i++) dist_matrix[i] = INFINITY;
    for (int i = 0; i < n; i++) dist_matrix[i * n + i] = 0.0;

    for (int i = 0; i < g->edge_count; i++) {
        Edge *e = &g->edges[i];
        int fi = 0, ti = 0;
        for (int j = 0; j < n; j++) {
            if (g->nodes[j].id == e->from_node) fi = j;
            if (g->nodes[j].id == e->to_node) ti = j;
        }
        dist_matrix[fi * n + ti] = 1.0; /* uniform weight by default */
    }

    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                double through_k = dist_matrix[i * n + k] + dist_matrix[k * n + j];
                if (through_k < dist_matrix[i * n + j])
                    dist_matrix[i * n + j] = through_k;
            }
        }
    }
    return n;
}

void floyd_warshall_print(PropertyGraph *g) {
    int n = graph_node_count(g);
    double *dist = calloc((size_t)(n * n), sizeof(double));
    if (!dist) return;
    floyd_warshall(g, dist, n * n);
    printf("\n=== Floyd-Warshall All-Pairs Shortest Paths ===\n");
    printf("     ");
    for (int j = 0; j < n; j++) printf("%4lld ", (long long)g->nodes[j].id);
    printf("\n");
    for (int i = 0; i < n; i++) {
        printf("%4lld ", (long long)g->nodes[i].id);
        for (int j = 0; j < n; j++) {
            if (dist[i * n + j] >= INFINITY / 2)
                printf("  INF");
            else
                printf("%5.1f", dist[i * n + j]);
        }
        printf("\n");
    }
    free(dist);
}

/* =========================================================================
 * Graph Coloring — Welsh-Powell Greedy Algorithm
 *
 * L5: Assigns colors to vertices such that no two adjacent vertices
 * share the same color, minimizing the number of colors used.
 * NP-hard in general; greedy with degree-sort is a 2-approximation
 * for some graph classes.
 *
 * Welsh-Powell (1967): sort vertices by descending degree, then
 * greedily assign the first available color.
 *
 * Applications: register allocation, exam scheduling, frequency
 * assignment in cellular networks, map coloring.
 * ========================================================================= */

int graph_coloring_greedy(PropertyGraph *g, int *colors, int max_nodes) {
    int n = graph_node_count(g);
    if (n == 0 || max_nodes < n) return 0;
    if (!colors) return 0;

    /* sort node indices by degree descending (Welsh-Powell) */
    typedef struct { int idx; int deg; } DegSort;
    DegSort *sorted = calloc((size_t)n, sizeof(DegSort));
    if (!sorted) return 0;
    for (int i = 0; i < n; i++) {
        sorted[i].idx = i;
        sorted[i].deg = node_out_degree(g, g->nodes[i].id);
    }
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (sorted[j].deg > sorted[i].deg) {
                DegSort tmp = sorted[i]; sorted[i] = sorted[j]; sorted[j] = tmp;
            }

    for (int i = 0; i < n; i++) colors[i] = -1;

    /* assign colors */
    for (int si = 0; si < n; si++) {
        int v = sorted[si].idx;
        bool used_colors[MAX_NODES] = {false};

        /* mark colors used by neighbors */
        AdjListNode *adj = g->adjacency[v].head;
        while (adj) {
            int w = 0;
            for (int i = 0; i < n; i++)
                if (g->nodes[i].id == adj->neighbor_id) { w = i; break; }
            if (colors[w] >= 0)
                used_colors[colors[w]] = true;
            adj = adj->next;
        }

        /* find first available color */
        int c = 0;
        while (c < MAX_NODES && used_colors[c]) c++;
        colors[v] = c;
    }

    int max_color = 0;
    for (int i = 0; i < n; i++)
        if (colors[i] > max_color) max_color = colors[i];

    free(sorted);
    return max_color + 1; /* chromatic number estimate */
}

int graph_coloring_print(PropertyGraph *g) {
    int n = graph_node_count(g);
    int *colors = calloc((size_t)n, sizeof(int));
    if (!colors) return 0;
    int chromatic = graph_coloring_greedy(g, colors, n);
    printf("\n=== Graph Coloring (Welsh-Powell): %d colors ===\n", chromatic);
    for (int c = 0; c < chromatic; c++) {
        printf("  Color %d: ", c);
        for (int i = 0; i < n; i++)
            if (colors[i] == c) printf("%lld ", (long long)g->nodes[i].id);
        printf("\n");
    }
    free(colors);
    return chromatic;
}
