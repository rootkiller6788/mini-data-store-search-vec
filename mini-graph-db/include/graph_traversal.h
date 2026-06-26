#ifndef GRAPH_TRAVERSAL_H
#define GRAPH_TRAVERSAL_H

#include "property_graph.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 256

typedef struct {
    int64_t node_ids[MAX_PATH_LENGTH];
    int64_t edge_ids[MAX_PATH_LENGTH];
    int length;
    double total_weight;
    bool found;
} PathResult;

typedef void (*TraversalVisitor)(int64_t node_id, int depth, void *user_data);

PathResult traverse_bfs(PropertyGraph *g, int64_t start, int64_t target);
PathResult traverse_dfs(PropertyGraph *g, int64_t start, int64_t target);
PathResult traverse_shortest_path(PropertyGraph *g, int64_t start, int64_t target);
PathResult traverse_dijkstra(PropertyGraph *g, int64_t start, int64_t target,
                             double (*weight_func)(Edge *e));
PathResult traverse_all_paths(PropertyGraph *g, int64_t start, int64_t target,
                              int max_depth);

int traverse_bfs_visitor(PropertyGraph *g, int64_t start,
                         TraversalVisitor visitor, void *user_data);
int traverse_dfs_visitor(PropertyGraph *g, int64_t start,
                         TraversalVisitor visitor, void *user_data);

void path_result_print(PathResult *pr);

#endif
