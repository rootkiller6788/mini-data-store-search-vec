#ifndef GRAPH_ALGO_H
#define GRAPH_ALGO_H

#include "property_graph.h"
#include <stddef.h>
#include <stdint.h>

#define PAGERANK_MAX_ITER   100
#define PAGERANK_DAMPING    0.85
#define PAGERANK_EPSILON    1e-6
#define MAX_COMPONENTS      1024
#define MAX_LABELS          64
#define MAX_MST_EDGES       4096
#define MAX_FLOW_NODES      256
#define MAX_SCC_NODES       4096

typedef struct {
    int64_t node_id;
    double score;
} RankedNode;

typedef struct {
    int64_t node_id;
    int label;
} LabelAssignment;

typedef struct {
    int64_t from;
    int64_t to;
    double weight;
} MstEdge;

typedef struct {
    int64_t *path;
    int path_len;
    double flow_value;
} MaxFlowResult;

/* PageRank (L5: Markov Chain Stationary Distribution) */
int pagerank(PropertyGraph *g, RankedNode *results, int max_results,
             double damping, int max_iter, double epsilon);
int pagerank_print_top(PropertyGraph *g, int top_n);

/* Label Propagation (L5: Semi-supervised Community Detection) */
int label_propagation(PropertyGraph *g, LabelAssignment *results,
                      int max_results, int max_iter);
int label_propagation_print(PropertyGraph *g);

/* Connected Components (L5: Union-Find based) */
int connected_components(PropertyGraph *g, int64_t *component_of,
                         int *component_count);
int connected_components_print(PropertyGraph *g);

/* Topological Sort (L5: Kahn's Algorithm) */
bool topological_sort(PropertyGraph *g, int64_t *sorted, int *count);
bool topological_sort_print(PropertyGraph *g);

/* Cycle Detection (L5: DFS-based backed edge detection) */
bool cycle_detection(PropertyGraph *g, int64_t *cycle, int *cycle_len);
bool cycle_detection_print(PropertyGraph *g);

/* Minimum Spanning Tree — Kruskal (L5: Greedy Algorithm, O(E log E))
 * Theorem (Kruskal, 1956): The greedy algorithm always yields an MST.
 * Proof: Exchange argument — replacing any edge in an optimal solution
 * with the next cheapest that doesn't create a cycle preserves optimality. */
int mst_kruskal(PropertyGraph *g, double (*weight_func)(Edge *e),
                MstEdge *mst, int max_edges);
double mst_kruskal_total_weight(MstEdge *mst, int count);
void mst_print(MstEdge *mst, int count);

/* Minimum Spanning Tree — Prim (L5: Greedy, O(V^2) with array, O(E log V) with heap)
 * Prim's algorithm builds the MST by growing a single tree, always adding
 * the minimum-weight edge connecting the tree to a new vertex.
 * Simpler than Kruskal for dense graphs. */
int mst_prim(PropertyGraph *g, double (*weight_func)(Edge *e),
             MstEdge *mst, int max_edges);

/* A* Search (L5: Informed Search, O(E) with good heuristic)
 * f(n) = g(n) + h(n) where g = cost from start, h = heuristic to target.
 * With admissible heuristic (h <= true distance), A* is optimal.
 * Reference: Hart, Nilsson, Raphael (1968). "A Formal Basis for the
 * Heuristic Determination of Minimum Cost Paths." */
int astar_search(PropertyGraph *g, int64_t start, int64_t target,
                 double (*weight_func)(Edge *e),
                 double (*heuristic)(int64_t node, int64_t target),
                 int64_t *path, int max_path);

/* Maximum Flow — Edmonds-Karp (L5: Ford-Fulkerson with BFS, O(VE^2))
 * Augmenting path algorithm using BFS to find the shortest augmenting path.
 * Guarantees termination and polynomial complexity.
 * Theorem (Max-Flow Min-Cut): max_flow = min_cut capacity.
 * Reference: Edmonds & Karp (1972). JACM 19(2):248-264. */
MaxFlowResult *max_flow_edmonds_karp(PropertyGraph *g, int64_t source,
                                      int64_t sink,
                                      double (*capacity_func)(Edge *e));
void max_flow_free(MaxFlowResult *r);

/* Strongly Connected Components — Kosaraju (L5: DFS-based, O(V+E))
 * Two-pass DFS: first on original graph, second on reversed graph
 * in order of decreasing finish times.
 * Reference: Aho, Hopcroft, Ullman (1974). Design & Analysis of Algorithms. */
int scc_kosaraju(PropertyGraph *g, int64_t *component_of, int *scc_count);
int scc_kosaraju_print(PropertyGraph *g);

/* Floyd-Warshall All-Pairs Shortest Paths (L5: DP, O(V^3))
 * Based on the optimal substructure: shortestPath(i, j, k) =
 * min(shortestPath(i, j, k-1),
 *     shortestPath(i, k, k-1) + shortestPath(k, j, k-1))
 * Reference: Floyd (1962), Warshall (1962). */
int floyd_warshall(PropertyGraph *g, double *dist_matrix, int max_nodes);
void floyd_warshall_print(PropertyGraph *g);

/* Graph Coloring — Greedy (L5: NP-hard in general, O(V^2+E) greedy)
 * Application: Register allocation, scheduling, frequency assignment.
 * Greedy: process nodes in some order, assign first available color.
 * Welsh-Powell heuristic: sort by degree descending. */
int graph_coloring_greedy(PropertyGraph *g, int *colors, int max_nodes);
int graph_coloring_print(PropertyGraph *g);

/* Utility */
int node_out_degree(PropertyGraph *g, int64_t node_id);
int node_in_degree(PropertyGraph *g, int64_t node_id);

#endif
