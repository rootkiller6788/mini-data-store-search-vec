#ifndef PROPERTY_GRAPH_H
#define PROPERTY_GRAPH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_NODE_LABELS      4
#define MAX_NODE_PROPERTIES  16
#define MAX_EDGE_PROPERTIES  8
#define MAX_LABEL_LEN        64
#define MAX_KEY_LEN          64
#define MAX_VALUE_LEN        256
#define MAX_EDGE_TYPE_LEN    64
#define HASH_TABLE_SIZE      1024
#define MAX_NODES            4096
#define MAX_EDGES            16384

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
} Property;

typedef struct {
    int64_t id;
    char labels[MAX_NODE_LABELS][MAX_LABEL_LEN];
    int label_count;
    Property properties[MAX_NODE_PROPERTIES];
    int property_count;
} Node;

typedef struct {
    int64_t id;
    char type[MAX_EDGE_TYPE_LEN];
    int64_t from_node;
    int64_t to_node;
    bool directed;
    Property properties[MAX_EDGE_PROPERTIES];
    int property_count;
} Edge;

typedef struct AdjListNode {
    int64_t edge_id;
    int64_t neighbor_id;
    struct AdjListNode *next;
} AdjListNode;

typedef struct AdjList {
    AdjListNode *head;
} AdjList;

typedef struct NodeHashEntry {
    int64_t node_id;
    int node_index;
    struct NodeHashEntry *next;
} NodeHashEntry;

typedef struct {
    Node nodes[MAX_NODES];
    int node_count;
    NodeHashEntry *node_hash[HASH_TABLE_SIZE];

    Edge edges[MAX_EDGES];
    int edge_count;

    AdjList *adjacency;
    int adjacency_capacity;
    int64_t next_node_id;
    int64_t next_edge_id;
} PropertyGraph;

PropertyGraph *graph_create(void);
void graph_destroy(PropertyGraph *g);

Node *graph_create_node(PropertyGraph *g);
Node *graph_get_node(PropertyGraph *g, int64_t id);
bool graph_node_add_label(Node *n, const char *label);
bool graph_node_set_property(Node *n, const char *key, const char *value);
const char *graph_node_get_property(Node *n, const char *key);

Edge *graph_create_edge(PropertyGraph *g, int64_t from, int64_t to,
                        const char *type, bool directed);
Edge *graph_get_edge(PropertyGraph *g, int64_t id);
bool graph_edge_set_property(Edge *e, const char *key, const char *value);

int graph_get_neighbors(PropertyGraph *g, int64_t node_id,
                        int64_t *neighbors, int max_neighbors);
int graph_get_edges_by_type(PropertyGraph *g, const char *type,
                            int64_t *edge_ids, int max_edges);
int graph_get_out_edges(PropertyGraph *g, int64_t node_id,
                        int64_t *edge_ids, int max_edges);
int graph_get_in_edges(PropertyGraph *g, int64_t node_id,
                       int64_t *edge_ids, int max_edges);
int graph_node_count(PropertyGraph *g);
int graph_edge_count(PropertyGraph *g);

#endif
