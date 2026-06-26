#include "graph_metrics.h"
#include "graph_algo.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* =========================================================================
 * Betweenness Centrality — Brandes' Algorithm (2001)
 *
 * L5: Algorithm — O(VE) for unweighted graphs.
 *
 * Theory: Betweenness centrality of node v is the sum, over all pairs
 * (s, t), of the fraction of shortest s-t paths that pass through v.
 *
 * C_B(v) = sum_{s != v != t} sigma_{st}(v) / sigma_{st}
 *
 * where sigma_{st} = total number of shortest s-t paths,
 *       sigma_{st}(v) = number of those paths passing through v.
 *
 * Brandes' insight: instead of O(V^3) all-pairs enumeration, use BFS
 * from each source s and accumulate dependencies backwards via:
 *
 *   delta_s(v) = sum_{w: v in pred(w)} sigma_{sv}/sigma_{sw} * (1 + delta_s(w))
 *
 * Reference: Brandes, U. (2001). "A Faster Algorithm for Betweenness
 *            Centrality." Journal of Mathematical Sociology 25(2):163-177.
 * ========================================================================= */

int centrality_betweenness(PropertyGraph *g, CentralityMetrics *results,
                           int max_results) {
    int n = graph_node_count(g);
    if (n == 0 || max_results <= 0) return 0;

    /* allocate per-source BFS structures */
    int *sigma = calloc((size_t)n, sizeof(int));
    int *dist = calloc((size_t)n, sizeof(int));
    double *delta = calloc((size_t)n, sizeof(double));
    double *cb = calloc((size_t)n, sizeof(double));
    int **pred = calloc((size_t)n, sizeof(int *));
    int *pred_count = calloc((size_t)n, sizeof(int));
    if (!sigma || !dist || !delta || !cb || !pred || !pred_count) {
        free(sigma); free(dist); free(delta); free(cb);
        free(pred); free(pred_count);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        pred[i] = calloc((size_t)n, sizeof(int));
        if (!pred[i]) {
            for (int j = 0; j < i; j++) free(pred[j]);
            free(sigma); free(dist); free(delta); free(cb);
            free(pred); free(pred_count);
            return 0;
        }
    }

    /* BFS from each node */
    for (int s = 0; s < n; s++) {
        for (int i = 0; i < n; i++) {
            sigma[i] = 0; dist[i] = -1; delta[i] = 0.0;
            pred_count[i] = 0;
        }
        sigma[s] = 1;
        dist[s] = 0;

        int64_t queue[MAX_NODES];
        int front = 0, rear = 0;
        queue[rear++] = (int64_t)s;

        /* forward BFS: compute sigma and shortest distances */
        int *stack = calloc((size_t)n, sizeof(int));
        int stack_top = 0;
        if (!stack) continue;

        while (front < rear) {
            int v = (int)queue[front++];
            stack[stack_top++] = v;

            AdjListNode *adj = g->adjacency[v].head;
            while (adj) {
                int w = 0;
                for (int i2 = 0; i2 < n; i2++) {
                    if (g->nodes[i2].id == adj->neighbor_id) { w = i2; break; }
                }
                if (dist[w] < 0) {
                    dist[w] = dist[v] + 1;
                    queue[rear++] = (int64_t)w;
                }
                if (dist[w] == dist[v] + 1) {
                    sigma[w] += sigma[v];
                    pred[w][pred_count[w]++] = v;
                }
                adj = adj->next;
            }
        }

        /* backward dependency accumulation */
        while (stack_top > 0) {
            int w = stack[--stack_top];
            for (int p = 0; p < pred_count[w]; p++) {
                int v = pred[w][p];
                delta[v] += (double)sigma[v] / (double)sigma[w] * (1.0 + delta[w]);
            }
            if (w != s)
                cb[w] += delta[w];
        }
        free(stack);
    }

    int count = (n < max_results) ? n : max_results;
    for (int i = 0; i < count; i++) {
        results[i].node_id = g->nodes[i].id;
        results[i].betweenness_centrality = cb[i];
    }
    /* sort by betweenness descending */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (results[j].betweenness_centrality > results[i].betweenness_centrality) {
                CentralityMetrics tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }

    for (int i = 0; i < n; i++) free(pred[i]);
    free(sigma); free(dist); free(delta); free(cb);
    free(pred); free(pred_count);
    return count;
}

/* =========================================================================
 * Degree Centrality
 *
 * L5: Simple but informative — C_D(v) = deg(v) / (n - 1)
 * Normalized to [0, 1]. Identifies hubs in the network.
 * ========================================================================= */

int centrality_degree(PropertyGraph *g, CentralityMetrics *results, int max_results) {
    int n = graph_node_count(g);
    if (n == 0 || max_results <= 0) return 0;
    int count = (n < max_results) ? n : max_results;
    double norm = (n > 1) ? 1.0 / (double)(n - 1) : 1.0;

    for (int i = 0; i < count; i++) {
        results[i].node_id = g->nodes[i].id;
        int deg = node_out_degree(g, g->nodes[i].id);
        results[i].degree_centrality = (double)deg * norm;
    }
    return count;
}

/* =========================================================================
 * Closeness Centrality
 *
 * L5: C_C(v) = (n - 1) / sum_{u != v} d(v, u)
 *
 * Uses BFS from each node. For disconnected graphs, only reachable
 * nodes contribute to the sum (Wasserman-Faust normalization).
 * ========================================================================= */

int centrality_closeness(PropertyGraph *g, CentralityMetrics *results, int max_results) {
    int n = graph_node_count(g);
    if (n == 0 || max_results <= 0) return 0;
    int count = (n < max_results) ? n : max_results;

    for (int s = 0; s < count; s++) {
        results[s].node_id = g->nodes[s].id;
        /* BFS to compute distances */
        int dist[MAX_NODES];
        for (int i = 0; i < n; i++) dist[i] = -1;
        dist[s] = 0;
        int64_t queue[MAX_NODES];
        int front = 0, rear = 0;
        queue[rear++] = (int64_t)s;
        while (front < rear) {
            int v = (int)queue[front++];
            AdjListNode *adj = g->adjacency[v].head;
            while (adj) {
                int w = 0;
                for (int i = 0; i < n; i++) {
                    if (g->nodes[i].id == adj->neighbor_id) { w = i; break; }
                }
                if (dist[w] < 0) {
                    dist[w] = dist[v] + 1;
                    queue[rear++] = (int64_t)w;
                }
                adj = adj->next;
            }
        }
        double sum = 0.0;
        int reachable = 0;
        for (int i = 0; i < n; i++) {
            if (i != s && dist[i] > 0) {
                sum += (double)dist[i];
                reachable++;
            }
        }
        if (sum > 0.0 && reachable > 0)
            results[s].closeness_centrality = (double)reachable / sum;
        else
            results[s].closeness_centrality = 0.0;
    }
    return count;
}

/* =========================================================================
 * Eigenvector Centrality — Power Iteration method
 *
 * L5: A*x = lambda*x  where A is the adjacency matrix.
 *
 * Power iteration: x^{(k+1)} = A*x^{(k)} / ||A*x^{(k)}||
 *
 * Converges to the dominant eigenvector under the Perron-Frobenius
 * theorem for irreducible non-negative matrices.
 * ========================================================================= */

int centrality_eigenvector(PropertyGraph *g, CentralityMetrics *results,
                           int max_results, int max_iter, double epsilon) {
    int n = graph_node_count(g);
    if (n == 0 || max_results <= 0) return 0;

    double *x = calloc((size_t)n, sizeof(double));
    double *x_new = calloc((size_t)n, sizeof(double));
    if (!x || !x_new) { free(x); free(x_new); return 0; }

    for (int i = 0; i < n; i++) x[i] = 1.0;

    for (int iter = 0; iter < max_iter; iter++) {
        for (int i = 0; i < n; i++) x_new[i] = 0.0;
        for (int i = 0; i < n; i++) {
            AdjListNode *adj = g->adjacency[i].head;
            while (adj) {
                int ni = 0;
                for (int j = 0; j < n; j++) {
                    if (g->nodes[j].id == adj->neighbor_id) { ni = j; break; }
                }
                x_new[ni] += x[i];
                adj = adj->next;
            }
        }
        double norm = 0.0;
        for (int i = 0; i < n; i++) norm += x_new[i] * x_new[i];
        norm = sqrt(norm);
        if (norm < 1e-12) break;
        for (int i = 0; i < n; i++) x_new[i] /= norm;

        double delta = 0.0;
        for (int i = 0; i < n; i++) {
            delta += fabs(x_new[i] - x[i]);
            x[i] = x_new[i];
        }
        if (delta < epsilon) break;
    }

    int count = (n < max_results) ? n : max_results;
    for (int i = 0; i < count; i++) {
        results[i].node_id = g->nodes[i].id;
        results[i].eigenvector_centrality = x[i];
    }
    free(x); free(x_new);
    return count;
}

int centrality_all(PropertyGraph *g, CentralityMetrics *results, int max_results) {
    int n = graph_node_count(g);
    if (n == 0) return 0;
    int count = (n < max_results) ? n : max_results;

    centrality_degree(g, results, count);

    /* betweenness is expensive — compute and merge */
    CentralityMetrics *btw = calloc((size_t)n, sizeof(CentralityMetrics));
    if (btw) {
        centrality_betweenness(g, btw, n);
        for (int i = 0; i < count; i++)
            results[i].betweenness_centrality = btw[i].betweenness_centrality;
        free(btw);
    }
    /* closeness separately allocated */
    CentralityMetrics *close = calloc((size_t)n, sizeof(CentralityMetrics));
    if (close) {
        centrality_closeness(g, close, n);
        for (int i = 0; i < count; i++)
            results[i].closeness_centrality = close[i].closeness_centrality;
        free(close);
    }
    CentralityMetrics *eig = calloc((size_t)n, sizeof(CentralityMetrics));
    if (eig) {
        centrality_eigenvector(g, eig, n, 100, 1e-6);
        for (int i = 0; i < count; i++)
            results[i].eigenvector_centrality = eig[i].eigenvector_centrality;
        free(eig);
    }
    return count;
}

void centrality_print_top(PropertyGraph *g, CentralityMetrics *results, int count) {
    printf("\n=== Centrality Metrics ===\n");
    printf("%5s %-8s %12s %12s %12s %12s\n",
           "Rank", "NodeID", "Degree", "Betweenness", "Closeness", "Eigenvector");
    for (int i = 0; i < count; i++) {
        printf("%5d %-8lld %12.6f %12.6f %12.6f %12.6f\n",
               i + 1, (long long)results[i].node_id,
               results[i].degree_centrality,
               results[i].betweenness_centrality,
               results[i].closeness_centrality,
               results[i].eigenvector_centrality);
    }
}

/* =========================================================================
 * Graph Statistics
 *
 * L5: Graph-level metrics that characterize the network topology.
 *
 * Density: d = E / (V*(V-1)) for directed, d = 2*E/(V*(V-1)) for undirected.
 *   Theorem (Coleman, 1964): Dense networks facilitate information flow
 *   and social capital formation.
 *
 * Global Clustering Coefficient (Transitivity):
 *   C = 3 * #triangles / #triplets
 *   Watts-Strogatz (1998): Real-world networks have high clustering.
 *
 * Diameter: max_{u,v} shortest_path(u,v).
 *   Milgram (1967): Real-world social networks exhibit 6-degree separation.
 * ========================================================================= */

double graph_density_calc(PropertyGraph *g) {
    int n = graph_node_count(g);
    int e = graph_edge_count(g);
    if (n <= 1) return 0.0;
    double denom = (double)n * (double)(n - 1);
    if (graph_is_directed(g))
        return (double)e / denom;
    else
        return 2.0 * (double)e / denom;
}

int graph_diameter(PropertyGraph *g) {
    int n = graph_node_count(g);
    if (n == 0) return 0;
    int max_dist = 0;

    for (int s = 0; s < n; s++) {
        int dist[MAX_NODES];
        for (int i = 0; i < n; i++) dist[i] = -1;
        dist[s] = 0;
        int64_t queue[MAX_NODES];
        int front = 0, rear = 0;
        queue[rear++] = (int64_t)s;
        while (front < rear) {
            int v = (int)queue[front++];
            AdjListNode *adj = g->adjacency[v].head;
            while (adj) {
                int w = 0;
                for (int i = 0; i < n; i++) {
                    if (g->nodes[i].id == adj->neighbor_id) { w = i; break; }
                }
                if (dist[w] < 0) {
                    dist[w] = dist[v] + 1;
                    if (dist[w] > max_dist) max_dist = dist[w];
                    queue[rear++] = (int64_t)w;
                }
                adj = adj->next;
            }
        }
        /* check for disconnected */
        for (int i = 0; i < n; i++) {
            if (dist[i] < 0) return -1; /* disconnected */
        }
    }
    return max_dist;
}

double graph_average_path_length(PropertyGraph *g) {
    int n = graph_node_count(g);
    if (n <= 1) return 0.0;
    double total = 0.0;
    int paths = 0;

    for (int s = 0; s < n; s++) {
        int dist[MAX_NODES];
        for (int i = 0; i < n; i++) dist[i] = -1;
        dist[s] = 0;
        int64_t queue[MAX_NODES];
        int front = 0, rear = 0;
        queue[rear++] = (int64_t)s;
        while (front < rear) {
            int v = (int)queue[front++];
            AdjListNode *adj = g->adjacency[v].head;
            while (adj) {
                int w = 0;
                for (int i = 0; i < n; i++) {
                    if (g->nodes[i].id == adj->neighbor_id) { w = i; break; }
                }
                if (dist[w] < 0) {
                    dist[w] = dist[v] + 1;
                    total += (double)dist[w];
                    paths++;
                    queue[rear++] = (int64_t)w;
                }
                adj = adj->next;
            }
        }
    }
    return paths > 0 ? total / (double)paths : 0.0;
}

/* Count triangles by checking for edges among neighbors of each node.
 * L5: Triangle Counting — O(V * d_max^2) where d_max is max degree.
 * Used for clustering coefficient and community analysis. */
int graph_triangle_count(PropertyGraph *g) {
    int n = graph_node_count(g);
    int triangles = 0;

    for (int i = 0; i < n; i++) {
        /* collect neighbors of node i */
        int64_t neighbors[MAX_NODES];
        int deg = graph_get_neighbors(g, g->nodes[i].id, neighbors, MAX_NODES);

        /* check if any pair of neighbors are also connected */
        for (int a = 0; a < deg; a++) {
            int a_idx = 0;
            for (int k = 0; k < n; k++)
                if (g->nodes[k].id == neighbors[a]) { a_idx = k; break; }
            int64_t a_neighbors[MAX_NODES];
            int a_deg = graph_get_neighbors(g, g->nodes[a_idx].id, a_neighbors, MAX_NODES);
            for (int b = a + 1; b < deg; b++) {
                for (int c = 0; c < a_deg; c++) {
                    if (a_neighbors[c] == neighbors[b]) {
                        triangles++;
                        break;
                    }
                }
            }
        }
    }
    /* For undirected graphs, each triangle is counted 3 times */
    return triangles / 3;
}

double node_local_clustering(PropertyGraph *g, int64_t node_id) {
    int n = graph_node_count(g);
    int idx = 0;
    for (int i = 0; i < n; i++)
        if (g->nodes[i].id == node_id) { idx = i; break; }

    int64_t neighbors[MAX_NODES];
    int deg = graph_get_neighbors(g, node_id, neighbors, MAX_NODES);
    if (deg < 2) return 0.0;

    int links_between = 0;
    for (int a = 0; a < deg; a++) {
        int a_idx = 0;
        for (int k = 0; k < n; k++)
            if (g->nodes[k].id == neighbors[a]) { a_idx = k; break; }
        int64_t a_neighbors[MAX_NODES];
        int a_deg = graph_get_neighbors(g, neighbors[a], a_neighbors, MAX_NODES);
        for (int b = a + 1; b < deg; b++) {
            for (int c = 0; c < a_deg; c++) {
                if (a_neighbors[c] == neighbors[b]) {
                    links_between++;
                    break;
                }
            }
        }
    }
    return (double)links_between / (0.5 * (double)deg * (double)(deg - 1));
}

double graph_global_clustering(PropertyGraph *g) {
    int triangles = graph_triangle_count(g);
    int n = graph_node_count(g);
    int triplets = 0;
    for (int i = 0; i < n; i++) {
        int64_t neighbors[MAX_NODES];
        int deg = graph_get_neighbors(g, g->nodes[i].id, neighbors, MAX_NODES);
        triplets += deg * (deg - 1) / 2;
    }
    return triplets > 0 ? 3.0 * (double)triangles / (double)triplets : 0.0;
}

int graph_statistics_compute(PropertyGraph *g, GraphStatistics *stats) {
    if (!g || !stats) return -1;
    memset(stats, 0, sizeof(GraphStatistics));
    stats->graph_density = graph_density_calc(g);
    stats->graph_diameter = graph_diameter(g);
    stats->average_path_length = graph_average_path_length(g);
    stats->global_clustering_coefficient = graph_global_clustering(g);
    stats->triangle_count = graph_triangle_count(g);
    /* Assortativity (Newman, 2002): Pearson correlation coefficient of
     * degrees at either end of an edge. Measures preference of nodes to
     * attach to similar-degree nodes. > 0 = assortative, < 0 = disassortative.
     *
     * r = (sum_e j*k - (1/E)*(sum_e (j+k)/2)^2) /
     *     (sum_e (j^2+k^2)/2 - (1/E)*(sum_e (j+k)/2)^2)
     * where j,k are degrees of the two nodes connected by edge e. */
    double sum_jk = 0.0, sum_jk_sq = 0.0, sum_jpk = 0.0;
    int ecount = graph_edge_count(g);
    int n = graph_node_count(g);
    for (int e = 0; e < ecount; e++) {
        Edge *edge = &g->edges[e];
        int j_deg = node_out_degree(g, edge->from_node);
        int k_deg = node_out_degree(g, edge->to_node);
        sum_jk += (double)(j_deg * k_deg);
        sum_jk_sq += (double)(j_deg * j_deg + k_deg * k_deg) / 2.0;
        sum_jpk += (double)(j_deg + k_deg) / 2.0;
    }
    if (ecount > 0) {
        double mean = sum_jpk / (double)ecount;
        double num = sum_jk / (double)ecount - mean * mean;
        double den = sum_jk_sq / (double)ecount - mean * mean;
        stats->assortativity = (den > 1e-12) ? num / den : 0.0;
    }
    return 0;
}

int graph_degree_distribution(PropertyGraph *g, DegreeHistogramEntry *hist, int max_bins) {
    if (!g || !hist || max_bins <= 0) return 0;
    memset(hist, 0, (size_t)max_bins * sizeof(DegreeHistogramEntry));
    int n = graph_node_count(g);

    /* count frequency of each degree */
    int freq[MAX_NODES] = {0};
    for (int i = 0; i < n; i++) {
        int deg = node_out_degree(g, g->nodes[i].id);
        if (deg < MAX_NODES) freq[deg]++;
    }

    int bin = 0;
    for (int d = 0; d < MAX_NODES && bin < max_bins; d++) {
        if (freq[d] > 0) {
            hist[bin].degree = d;
            hist[bin].count = freq[d];
            bin++;
        }
    }
    return bin;
}

bool graph_is_directed(PropertyGraph *g) {
    if (!g || g->edge_count == 0) return false;
    return g->edges[0].directed;
}

int graph_max_degree(PropertyGraph *g) {
    int n = graph_node_count(g), maxd = 0;
    for (int i = 0; i < n; i++) {
        int d = node_out_degree(g, g->nodes[i].id);
        if (d > maxd) maxd = d;
    }
    return maxd;
}

int graph_min_degree(PropertyGraph *g) {
    int n = graph_node_count(g);
    if (n == 0) return 0;
    int mind = MAX_NODES;
    for (int i = 0; i < n; i++) {
        int d = node_out_degree(g, g->nodes[i].id);
        if (d < mind) mind = d;
    }
    return (mind == MAX_NODES) ? 0 : mind;
}

void graph_statistics_print(GraphStatistics *stats) {
    if (!stats) return;
    printf("\n=== Graph Statistics ===\n");
    printf("  Density:                    %.6f\n", stats->graph_density);
    printf("  Diameter:                   %d\n", stats->graph_diameter);
    printf("  Average Path Length:        %.4f\n", stats->average_path_length);
    printf("  Global Clustering Coeff:    %.6f\n", stats->global_clustering_coefficient);
    printf("  Triangle Count:             %d\n", stats->triangle_count);
    printf("  Assortativity (deg corr):   %.4f\n", stats->assortativity);
}

void degree_distribution_print(PropertyGraph *g) {
    DegreeHistogramEntry hist[MAX_DEGREE_HISTOGRAM];
    int bins = graph_degree_distribution(g, hist, MAX_DEGREE_HISTOGRAM);
    printf("\n=== Degree Distribution ===\n");
    for (int i = 0; i < bins; i++)
        printf("  k=%d: %d nodes\n", hist[i].degree, hist[i].count);
}
