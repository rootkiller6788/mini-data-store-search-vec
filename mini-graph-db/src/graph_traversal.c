#include "graph_traversal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    int64_t node_id;
    int64_t came_from_edge;
    int64_t came_from_node;
    double distance;
    bool visited;
} DijkstraState;

static int find_dijkstra_state(DijkstraState *states, int count, int64_t id) {
    for (int i = 0; i < count; i++) {
        if (states[i].node_id == id) return i;
    }
    return -1;
}

static int64_t extract_min(DijkstraState *states, int count) {
    int64_t best_id = -1;
    double best_dist = INFINITY;
    for (int i = 0; i < count; i++) {
        if (!states[i].visited && states[i].distance < best_dist) {
            best_dist = states[i].distance;
            best_id = states[i].node_id;
        }
    }
    return best_id;
}

static void reconstruct_path(PropertyGraph *g, DijkstraState *states,
                             int state_count, int64_t start, int64_t target,
                             PathResult *result) {
    memset(result, 0, sizeof(PathResult));
    int idx = find_dijkstra_state(states, state_count, target);
    if (idx < 0 || states[idx].distance >= INFINITY) {
        result->found = false;
        return;
    }
    result->found = true;
    result->total_weight = states[idx].distance;

    int64_t node_ids_temp[MAX_PATH_LENGTH];
    int64_t edge_ids_temp[MAX_PATH_LENGTH];
    int len = 0;
    int64_t cur = target;
    while (cur != start && len < MAX_PATH_LENGTH) {
        int ci = find_dijkstra_state(states, state_count, cur);
        if (ci < 0) break;
        node_ids_temp[len] = cur;
        edge_ids_temp[len] = states[ci].came_from_edge;
        len++;
        cur = states[ci].came_from_node;
    }
    node_ids_temp[len] = start;
    len++;

    for (int i = 0; i < len; i++) {
        result->node_ids[i] = node_ids_temp[len - 1 - i];
        if (i < len - 1)
            result->edge_ids[i] = edge_ids_temp[len - 2 - i];
    }
    result->length = len;
}

static PathResult bfs_internal(PropertyGraph *g, int64_t start, int64_t target,
                               int max_nodes_to_visit) {
    PathResult result;
    memset(&result, 0, sizeof(PathResult));

    if (graph_get_node(g, start) == NULL) return result;

    int64_t queue[MAX_NODES];
    int64_t parent[MAX_NODES];
    int64_t parent_edge[MAX_NODES];
    bool visited_map[MAX_NODES] = {false};

    int front = 0, rear = 0;
    int node_count = graph_node_count(g);

    queue[rear++] = start;
    int si = 0;
    for (int i = 0; i < node_count; i++) {
        if (g->nodes[i].id == start) { si = i; break; }
    }
    visited_map[si] = true;
    parent[si] = -1;
    parent_edge[si] = -1;

    int visited_count = 0;
    while (front < rear && visited_count < max_nodes_to_visit) {
        int64_t cur = queue[front++];
        int ci = 0;
        for (int i = 0; i < node_count; i++) {
            if (g->nodes[i].id == cur) { ci = i; break; }
        }
        visited_count++;

        if (cur == target && target >= 0) break;

        if (ci >= g->adjacency_capacity) continue;
        AdjListNode *adj = g->adjacency[ci].head;
        while (adj) {
            int ni = 0;
            for (int i = 0; i < node_count; i++) {
                if (g->nodes[i].id == adj->neighbor_id) { ni = i; break; }
            }
            if (!visited_map[ni]) {
                visited_map[ni] = true;
                parent[ni] = ci;
                parent_edge[ni] = adj->edge_id;
                queue[rear++] = adj->neighbor_id;
            }
            adj = adj->next;
        }
    }

    if (target < 0) {
        result.found = true;
        result.length = rear;
        for (int i = 0; i < rear; i++)
            result.node_ids[i] = queue[i];
        return result;
    }

    int ti = 0;
    for (int i = 0; i < node_count; i++) {
        if (g->nodes[i].id == target) { ti = i; break; }
    }
    if (!visited_map[ti]) return result;

    int64_t rev_nodes[MAX_PATH_LENGTH];
    int64_t rev_edges[MAX_PATH_LENGTH];
    int len = 0;
    int cur_i = ti;
    while (cur_i != si && len < MAX_PATH_LENGTH) {
        rev_nodes[len] = g->nodes[cur_i].id;
        rev_edges[len] = parent_edge[cur_i];
        len++;
        cur_i = parent[cur_i];
    }
    rev_nodes[len] = g->nodes[si].id;
    len++;

    for (int i = 0; i < len; i++) {
        result.node_ids[i] = rev_nodes[len - 1 - i];
        if (i < len - 1) result.edge_ids[i] = rev_edges[len - 2 - i];
    }
    result.length = len;
    result.found = true;
    return result;
}

PathResult traverse_bfs(PropertyGraph *g, int64_t start, int64_t target) {
    return bfs_internal(g, start, target, MAX_NODES);
}

PathResult traverse_shortest_path(PropertyGraph *g, int64_t start, int64_t target) {
    return bfs_internal(g, start, target, MAX_NODES);
}

PathResult traverse_dfs(PropertyGraph *g, int64_t start, int64_t target) {
    PathResult result;
    memset(&result, 0, sizeof(PathResult));

    if (graph_get_node(g, start) == NULL) return result;

    int64_t stack[MAX_NODES];
    int64_t parent[MAX_NODES];
    int64_t parent_edge[MAX_NODES];
    bool visited_map[MAX_NODES] = {false};
    int64_t order[MAX_NODES];
    int order_len = 0;
    int node_count = graph_node_count(g);

    int top = 0;
    stack[top++] = start;
    int si = 0;
    for (int i = 0; i < node_count; i++) {
        if (g->nodes[i].id == start) { si = i; break; }
    }
    parent[si] = -1;
    parent_edge[si] = -1;

    while (top > 0) {
        int64_t cur = stack[--top];
        int ci = 0;
        for (int i = 0; i < node_count; i++) {
            if (g->nodes[i].id == cur) { ci = i; break; }
        }

        if (visited_map[ci]) continue;
        visited_map[ci] = true;
        if (order_len < MAX_NODES) order[order_len++] = cur;

        if (cur == target && target >= 0) break;

        AdjListNode *adj = g->adjacency[ci].head;
        int neighbor_count = 0;
        int64_t neighbors[MAX_NODES];
        int64_t neighbor_edges[MAX_NODES];
        while (adj) {
            int ni = 0;
            for (int i = 0; i < node_count; i++) {
                if (g->nodes[i].id == adj->neighbor_id) { ni = i; break; }
            }
            if (!visited_map[ni] && neighbor_count < MAX_NODES) {
                neighbors[neighbor_count] = adj->neighbor_id;
                neighbor_edges[neighbor_count] = adj->edge_id;
                neighbor_count++;
            }
            adj = adj->next;
        }
        for (int k = neighbor_count - 1; k >= 0; k--) {
            int ni = 0;
            for (int i = 0; i < node_count; i++) {
                if (g->nodes[i].id == neighbors[k]) { ni = i; break; }
            }
            parent[ni] = ci;
            parent_edge[ni] = neighbor_edges[k];
            stack[top++] = neighbors[k];
        }
    }

    if (target >= 0) {
        int ti = 0;
        for (int i = 0; i < node_count; i++) {
            if (g->nodes[i].id == target) { ti = i; break; }
        }
        if (!visited_map[ti]) return result;

        int64_t rev_nodes[MAX_PATH_LENGTH];
        int64_t rev_edges[MAX_PATH_LENGTH];
        int len = 0;
        int cur_i = ti;
        while (cur_i != si && len < MAX_PATH_LENGTH) {
            rev_nodes[len] = g->nodes[cur_i].id;
            rev_edges[len] = parent_edge[cur_i];
            len++;
            cur_i = parent[cur_i];
        }
        rev_nodes[len] = g->nodes[si].id;
        len++;
        for (int i = 0; i < len; i++) {
            result.node_ids[i] = rev_nodes[len - 1 - i];
            if (i < len - 1) result.edge_ids[i] = rev_edges[len - 2 - i];
        }
        result.length = len;
        result.found = true;
    } else {
        result.found = true;
        result.length = order_len;
        memcpy(result.node_ids, order, (size_t)order_len * sizeof(int64_t));
    }
    return result;
}

PathResult traverse_dijkstra(PropertyGraph *g, int64_t start, int64_t target,
                             double (*weight_func)(Edge *e)) {
    PathResult result;
    memset(&result, 0, sizeof(PathResult));

    int node_count = graph_node_count(g);
    DijkstraState *states = calloc((size_t)node_count, sizeof(DijkstraState));
    if (!states) return result;

    for (int i = 0; i < node_count; i++) {
        states[i].node_id = g->nodes[i].id;
        states[i].distance = INFINITY;
        states[i].came_from_node = -1;
        states[i].came_from_edge = -1;
    }
    int si = find_dijkstra_state(states, node_count, start);
    if (si >= 0) states[si].distance = 0.0;

    for (int round = 0; round < node_count; round++) {
        int64_t u_id = extract_min(states, node_count);
        if (u_id < 0) break;
        int ui = find_dijkstra_state(states, node_count, u_id);
        if (ui < 0) continue;
        states[ui].visited = true;

        if (u_id == target) break;

        AdjListNode *adj = g->adjacency[ui].head;
        while (adj) {
            int vi = find_dijkstra_state(states, node_count, adj->neighbor_id);
            if (vi < 0) { adj = adj->next; continue; }
            if (states[vi].visited) { adj = adj->next; continue; }

            double w = 1.0;
            if (weight_func) {
                Edge *e = graph_get_edge(g, adj->edge_id);
                if (e) w = weight_func(e);
            }

            double alt = states[ui].distance + w;
            if (alt < states[vi].distance) {
                states[vi].distance = alt;
                states[vi].came_from_node = u_id;
                states[vi].came_from_edge = adj->edge_id;
            }
            adj = adj->next;
        }
    }

    reconstruct_path(g, states, node_count, start, target, &result);
    free(states);
    return result;
}

static void dfs_all_paths(PropertyGraph *g, int64_t cur, int64_t target,
                          int64_t *path, int64_t *edge_path, int depth,
                          bool *visited_map, int node_count,
                          PathResult *best, int max_depth) {
    int ci = 0;
    for (int i = 0; i < node_count; i++) {
        if (g->nodes[i].id == cur) { ci = i; break; }
    }
    if (depth > max_depth) return;
    if (cur == target) {
        if (!best->found || depth < best->length) {
            best->found = true;
            best->length = depth + 1;
            for (int i = 0; i <= depth; i++) {
                best->node_ids[i] = path[i];
                if (i < depth) best->edge_ids[i] = edge_path[i];
            }
        }
        return;
    }
    visited_map[ci] = true;
    AdjListNode *adj = g->adjacency[ci].head;
    while (adj) {
        int ni = 0;
        for (int i = 0; i < node_count; i++) {
            if (g->nodes[i].id == adj->neighbor_id) { ni = i; break; }
        }
        if (!visited_map[ni]) {
            path[depth + 1] = adj->neighbor_id;
            edge_path[depth] = adj->edge_id;
            dfs_all_paths(g, adj->neighbor_id, target, path, edge_path,
                         depth + 1, visited_map, node_count, best, max_depth);
        }
        adj = adj->next;
    }
    visited_map[ci] = false;
}

PathResult traverse_all_paths(PropertyGraph *g, int64_t start, int64_t target,
                              int max_depth) {
    PathResult result;
    memset(&result, 0, sizeof(PathResult));
    int node_count = graph_node_count(g);

    bool *visited_map = calloc((size_t)node_count, sizeof(bool));
    if (!visited_map) return result;

    int64_t *path = calloc((size_t)(max_depth + 2), sizeof(int64_t));
    int64_t *edge_path = calloc((size_t)(max_depth + 2), sizeof(int64_t));
    if (!path || !edge_path) {
        free(visited_map); free(path); free(edge_path);
        return result;
    }
    path[0] = start;
    dfs_all_paths(g, start, target, path, edge_path, 0, visited_map,
                  node_count, &result, max_depth);

    free(visited_map); free(path); free(edge_path);
    return result;
}

int traverse_bfs_visitor(PropertyGraph *g, int64_t start,
                         TraversalVisitor visitor, void *user_data) {
    int node_count = graph_node_count(g);
    bool *visited = calloc((size_t)node_count, sizeof(bool));
    if (!visited) return 0;

    int64_t queue[MAX_NODES];
    int depth[MAX_NODES];
    int front = 0, rear = 0;
    queue[rear] = start;
    depth[rear] = 0;
    rear++;
    int si = 0;
    for (int i = 0; i < node_count; i++) {
        if (g->nodes[i].id == start) { si = i; break; }
    }
    visited[si] = true;

    int count = 0;
    while (front < rear) {
        int64_t cur = queue[front];
        int d = depth[front];
        front++;
        visitor(cur, d, user_data);
        count++;

        int ci = 0;
        for (int i = 0; i < node_count; i++) {
            if (g->nodes[i].id == cur) { ci = i; break; }
        }
        AdjListNode *adj = g->adjacency[ci].head;
        while (adj) {
            int ni = 0;
            for (int i = 0; i < node_count; i++) {
                if (g->nodes[i].id == adj->neighbor_id) { ni = i; break; }
            }
            if (!visited[ni]) {
                visited[ni] = true;
                queue[rear] = adj->neighbor_id;
                depth[rear] = d + 1;
                rear++;
            }
            adj = adj->next;
        }
    }
    free(visited);
    return count;
}

int traverse_dfs_visitor(PropertyGraph *g, int64_t start,
                         TraversalVisitor visitor, void *user_data) {
    int node_count = graph_node_count(g);
    bool *visited = calloc((size_t)node_count, sizeof(bool));
    if (!visited) return 0;

    int64_t stack[MAX_NODES];
    int stack_depth[MAX_NODES];
    int top = 0;
    stack[top] = start;
    stack_depth[top] = 0;
    top++;

    int count = 0;
    while (top > 0) {
        top--;
        int64_t cur = stack[top];
        int d = stack_depth[top];

        int ci = 0;
        for (int i = 0; i < node_count; i++) {
            if (g->nodes[i].id == cur) { ci = i; break; }
        }
        if (visited[ci]) continue;
        visited[ci] = true;
        visitor(cur, d, user_data);
        count++;

        AdjListNode *adj = g->adjacency[ci].head;
        int nc = 0;
        int64_t neighs[MAX_NODES];
        int64_t neigh_edges[MAX_NODES];
        while (adj) {
            int ni = 0;
            for (int i = 0; i < node_count; i++) {
                if (g->nodes[i].id == adj->neighbor_id) { ni = i; break; }
            }
            if (!visited[ni] && nc < MAX_NODES) {
                neighs[nc] = adj->neighbor_id;
                neigh_edges[nc] = adj->edge_id;
                nc++;
            }
            adj = adj->next;
        }
        for (int k = nc - 1; k >= 0; k--) {
            stack[top] = neighs[k];
            stack_depth[top] = d + 1;
            top++;
        }
    }
    free(visited);
    return count;
}

void path_result_print(PathResult *pr) {
    if (!pr->found) {
        printf("Path: NOT FOUND\n");
        return;
    }
    printf("Path: ");
    for (int i = 0; i < pr->length; i++) {
        printf("%lld", (long long)pr->node_ids[i]);
        if (i < pr->length - 1) printf(" -> ");
    }
    printf("\n  length=%d  weight=%.4f\n", pr->length, pr->total_weight);
}
