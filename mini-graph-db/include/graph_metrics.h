#ifndef GRAPH_METRICS_H
#define GRAPH_METRICS_H

#include "property_graph.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * L5: Algorithms — Graph Centrality and Statistics
 *
 * Centrality measures quantify node importance in a network.
 *   - Degree Centrality: fraction of nodes a node connects to.
 *   - Betweenness Centrality (Brandes, 2001): fraction of shortest paths
 *     that pass through a node. O(VE) for unweighted, O(VE + V^2 log V)
 *     for weighted.
 *   - Closeness Centrality: inverse of sum of distances to all others.
 *   - Eigenvector Centrality: node importance by neighbor importance.
 *
 * Graph Statistics:
 *   - Clustering Coefficient (Watts-Strogatz): local = fraction of
 *     neighbor pairs that are connected. Global = 3*triangles/triplets.
 *   - Graph Density: E / (V*(V-1)) for directed, 2*E/(V*(V-1)) undirected.
 *   - Graph Diameter: longest shortest path (max eccentricity).
 *   - Degree Distribution: frequency of each degree in the graph.
 *
 * L7: Applications — Graph Analytics for:
 *   - Social network analysis (influencer detection)
 *   - Web graph analysis (authoritative page identification)
 *   - Biological network analysis (protein interaction hubs)
 * ============================================================================ */

#define MAX_DEGREE_HISTOGRAM 64

typedef struct {
    double degree_centrality;
    double betweenness_centrality;
    double closeness_centrality;
    double eigenvector_centrality;
    int64_t node_id;
} CentralityMetrics;

typedef struct {
    int degree;
    int count;
} DegreeHistogramEntry;

typedef struct {
    double global_clustering_coefficient;
    double graph_density;
    int graph_diameter;
    double average_path_length;
    int triangle_count;
    double assortativity;
} GraphStatistics;

/* Centrality */
int centrality_degree(PropertyGraph *g, CentralityMetrics *results, int max_results);
int centrality_betweenness(PropertyGraph *g, CentralityMetrics *results, int max_results);
int centrality_closeness(PropertyGraph *g, CentralityMetrics *results, int max_results);
int centrality_eigenvector(PropertyGraph *g, CentralityMetrics *results,
                           int max_results, int max_iter, double epsilon);
int centrality_all(PropertyGraph *g, CentralityMetrics *results, int max_results);
void centrality_print_top(PropertyGraph *g, CentralityMetrics *results,
                          int count);

/* Graph Statistics */
int graph_statistics_compute(PropertyGraph *g, GraphStatistics *stats);
int graph_diameter(PropertyGraph *g);
double graph_average_path_length(PropertyGraph *g);
double graph_global_clustering(PropertyGraph *g);
int graph_triangle_count(PropertyGraph *g);
double graph_density_calc(PropertyGraph *g);
int graph_degree_distribution(PropertyGraph *g, DegreeHistogramEntry *hist, int max_bins);
void graph_statistics_print(GraphStatistics *stats);
void degree_distribution_print(PropertyGraph *g);

/* Clustering */
double node_local_clustering(PropertyGraph *g, int64_t node_id);
double graph_global_clustering(PropertyGraph *g);

/* Utility */
bool graph_is_directed(PropertyGraph *g);
int  graph_max_degree(PropertyGraph *g);
int  graph_min_degree(PropertyGraph *g);

#endif
