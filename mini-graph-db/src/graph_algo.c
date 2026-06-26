#include "graph_algo.h"
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
