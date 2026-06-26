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

typedef struct {
    int64_t node_id;
    double score;
} RankedNode;

typedef struct {
    int64_t node_id;
    int label;
} LabelAssignment;

int pagerank(PropertyGraph *g, RankedNode *results, int max_results,
             double damping, int max_iter, double epsilon);
int pagerank_print_top(PropertyGraph *g, int top_n);

int label_propagation(PropertyGraph *g, LabelAssignment *results,
                      int max_results, int max_iter);
int label_propagation_print(PropertyGraph *g);

int connected_components(PropertyGraph *g, int64_t *component_of,
                         int *component_count);
int connected_components_print(PropertyGraph *g);

bool topological_sort(PropertyGraph *g, int64_t *sorted, int *count);
bool topological_sort_print(PropertyGraph *g);

bool cycle_detection(PropertyGraph *g, int64_t *cycle, int *cycle_len);
bool cycle_detection_print(PropertyGraph *g);

int node_out_degree(PropertyGraph *g, int64_t node_id);
int node_in_degree(PropertyGraph *g, int64_t node_id);

#endif
