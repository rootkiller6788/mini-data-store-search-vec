#include "property_graph.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned int hash_int64(int64_t key) {
    unsigned int h = (unsigned int)(key ^ (key >> 32));
    h = ((h >> 16) ^ h) * 0x45d9f3bU;
    h = ((h >> 16) ^ h) * 0x45d9f3bU;
    h = (h >> 16) ^ h;
    return h % HASH_TABLE_SIZE;
}

PropertyGraph *graph_create(void) {
    PropertyGraph *g = calloc(1, sizeof(PropertyGraph));
    if (!g) return NULL;
    g->adjacency_capacity = MAX_NODES;
    g->adjacency = calloc((size_t)g->adjacency_capacity, sizeof(AdjList));
    if (!g->adjacency) { free(g); return NULL; }
    g->next_node_id = 1;
    g->next_edge_id = 1;
    return g;
}

void graph_destroy(PropertyGraph *g) {
    if (!g) return;
    for (int i = 0; i < g->adjacency_capacity && i < g->node_count; i++) {
        AdjListNode *cur = g->adjacency[i].head;
        while (cur) {
            AdjListNode *tmp = cur;
            cur = cur->next;
            free(tmp);
        }
    }
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        NodeHashEntry *cur = g->node_hash[i];
        while (cur) {
            NodeHashEntry *tmp = cur;
            cur = cur->next;
            free(tmp);
        }
    }
    free(g->adjacency);
    free(g);
}

static void hash_insert(PropertyGraph *g, int64_t id, int index) {
    unsigned int bucket = hash_int64(id);
    NodeHashEntry *entry = malloc(sizeof(NodeHashEntry));
    if (!entry) return;
    entry->node_id = id;
    entry->node_index = index;
    entry->next = g->node_hash[bucket];
    g->node_hash[bucket] = entry;
}

static int hash_lookup(PropertyGraph *g, int64_t id) {
    unsigned int bucket = hash_int64(id);
    for (NodeHashEntry *cur = g->node_hash[bucket]; cur; cur = cur->next) {
        if (cur->node_id == id) return cur->node_index;
    }
    return -1;
}

Node *graph_create_node(PropertyGraph *g) {
    if (g->node_count >= MAX_NODES) return NULL;
    int idx = g->node_count;
    Node *n = &g->nodes[idx];
    memset(n, 0, sizeof(Node));
    n->id = g->next_node_id++;
    g->node_count++;
    hash_insert(g, n->id, idx);
    if (idx >= g->adjacency_capacity) {
        int new_cap = g->adjacency_capacity * 2;
        AdjList *new_adj = realloc(g->adjacency, (size_t)new_cap * sizeof(AdjList));
        if (!new_adj) return NULL;
        memset(new_adj + g->adjacency_capacity, 0,
               (size_t)(new_cap - g->adjacency_capacity) * sizeof(AdjList));
        g->adjacency = new_adj;
        g->adjacency_capacity = new_cap;
    }
    return n;
}

Node *graph_get_node(PropertyGraph *g, int64_t id) {
    int idx = hash_lookup(g, id);
    if (idx < 0) return NULL;
    return &g->nodes[idx];
}

bool graph_node_add_label(Node *n, const char *label) {
    if (n->label_count >= MAX_NODE_LABELS) return false;
    strncpy(n->labels[n->label_count], label, MAX_LABEL_LEN - 1);
    n->labels[n->label_count][MAX_LABEL_LEN - 1] = '\0';
    n->label_count++;
    return true;
}

bool graph_node_set_property(Node *n, const char *key, const char *value) {
    for (int i = 0; i < n->property_count; i++) {
        if (strcmp(n->properties[i].key, key) == 0) {
            strncpy(n->properties[i].value, value, MAX_VALUE_LEN - 1);
            n->properties[i].value[MAX_VALUE_LEN - 1] = '\0';
            return true;
        }
    }
    if (n->property_count >= MAX_NODE_PROPERTIES) return false;
    strncpy(n->properties[n->property_count].key, key, MAX_KEY_LEN - 1);
    n->properties[n->property_count].key[MAX_KEY_LEN - 1] = '\0';
    strncpy(n->properties[n->property_count].value, value, MAX_VALUE_LEN - 1);
    n->properties[n->property_count].value[MAX_VALUE_LEN - 1] = '\0';
    n->property_count++;
    return true;
}

const char *graph_node_get_property(Node *n, const char *key) {
    for (int i = 0; i < n->property_count; i++) {
        if (strcmp(n->properties[i].key, key) == 0)
            return n->properties[i].value;
    }
    return NULL;
}

Edge *graph_create_edge(PropertyGraph *g, int64_t from, int64_t to,
                        const char *type, bool directed) {
    if (g->edge_count >= MAX_EDGES) return NULL;
    int from_idx = hash_lookup(g, from);
    int to_idx = hash_lookup(g, to);
    if (from_idx < 0 || to_idx < 0) return NULL;

    Edge *e = &g->edges[g->edge_count];
    memset(e, 0, sizeof(Edge));
    e->id = g->next_edge_id++;
    e->from_node = from;
    e->to_node = to;
    e->directed = directed;
    strncpy(e->type, type, MAX_EDGE_TYPE_LEN - 1);
    e->type[MAX_EDGE_TYPE_LEN - 1] = '\0';

    AdjListNode *adj_node = malloc(sizeof(AdjListNode));
    if (!adj_node) return NULL;
    adj_node->edge_id = e->id;
    adj_node->neighbor_id = to;
    adj_node->next = g->adjacency[from_idx].head;
    g->adjacency[from_idx].head = adj_node;

    if (!directed) {
        AdjListNode *rev_node = malloc(sizeof(AdjListNode));
        if (rev_node) {
            rev_node->edge_id = e->id;
            rev_node->neighbor_id = from;
            rev_node->next = g->adjacency[to_idx].head;
            g->adjacency[to_idx].head = rev_node;
        }
    }

    g->edge_count++;
    return e;
}

Edge *graph_get_edge(PropertyGraph *g, int64_t id) {
    for (int i = 0; i < g->edge_count; i++) {
        if (g->edges[i].id == id) return &g->edges[i];
    }
    return NULL;
}

bool graph_edge_set_property(Edge *e, const char *key, const char *value) {
    for (int i = 0; i < e->property_count; i++) {
        if (strcmp(e->properties[i].key, key) == 0) {
            strncpy(e->properties[i].value, value, MAX_VALUE_LEN - 1);
            e->properties[i].value[MAX_VALUE_LEN - 1] = '\0';
            return true;
        }
    }
    if (e->property_count >= MAX_EDGE_PROPERTIES) return false;
    strncpy(e->properties[e->property_count].key, key, MAX_KEY_LEN - 1);
    e->properties[e->property_count].key[MAX_KEY_LEN - 1] = '\0';
    strncpy(e->properties[e->property_count].value, value, MAX_VALUE_LEN - 1);
    e->properties[e->property_count].value[MAX_VALUE_LEN - 1] = '\0';
    e->property_count++;
    return true;
}

int graph_get_neighbors(PropertyGraph *g, int64_t node_id,
                        int64_t *neighbors, int max_neighbors) {
    int idx = hash_lookup(g, node_id);
    if (idx < 0) return 0;
    int count = 0;
    AdjListNode *cur = g->adjacency[idx].head;
    while (cur && count < max_neighbors) {
        neighbors[count++] = cur->neighbor_id;
        cur = cur->next;
    }
    return count;
}

int graph_get_edges_by_type(PropertyGraph *g, const char *type,
                            int64_t *edge_ids, int max_edges) {
    int count = 0;
    for (int i = 0; i < g->edge_count && count < max_edges; i++) {
        if (strcmp(g->edges[i].type, type) == 0)
            edge_ids[count++] = g->edges[i].id;
    }
    return count;
}

int graph_get_out_edges(PropertyGraph *g, int64_t node_id,
                        int64_t *edge_ids, int max_edges) {
    int idx = hash_lookup(g, node_id);
    if (idx < 0) return 0;
    int count = 0;
    AdjListNode *cur = g->adjacency[idx].head;
    while (cur && count < max_edges) {
        edge_ids[count++] = cur->edge_id;
        cur = cur->next;
    }
    return count;
}

int graph_get_in_edges(PropertyGraph *g, int64_t node_id,
                       int64_t *edge_ids, int max_edges) {
    int count = 0;
    for (int i = 0; count < max_edges && i < g->edge_count; i++) {
        if (g->edges[i].to_node == node_id)
            edge_ids[count++] = g->edges[i].id;
    }
    return count;
}

int graph_node_count(PropertyGraph *g) { return g->node_count; }
int graph_edge_count(PropertyGraph *g) { return g->edge_count; }
