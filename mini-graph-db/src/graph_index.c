#include "graph_index.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

NodeIndex *index_create_node(PropertyGraph *g) {
    NodeIndex *idx = calloc(1, sizeof(NodeIndex));
    if (!idx) return NULL;

    int n = graph_node_count(g);
    for (int i = 0; i < n; i++) {
        Node *node = &g->nodes[i];
        for (int l = 0; l < node->label_count; l++) {
            bool found = false;
            for (int e = 0; e < idx->entry_count; e++) {
                if (strcmp(idx->entries[e].label, node->labels[l]) == 0) {
                    NodeIdList *list = &idx->entries[e].nodes;
                    if (list->count < MAX_INDEX_ENTRIES)
                        list->node_ids[list->count++] = node->id;
                    found = true;
                    break;
                }
            }
            if (!found && idx->entry_count < INDEX_BUCKETS) {
                LabelIndexEntry *entry = &idx->entries[idx->entry_count++];
                strncpy(entry->label, node->labels[l], MAX_LABEL_LEN - 1);
                entry->label[MAX_LABEL_LEN - 1] = '\0';
                entry->nodes.node_ids[0] = node->id;
                entry->nodes.count = 1;
            }
        }
    }
    return idx;
}

NodeIdList index_lookup_by_label(NodeIndex *idx, const char *label) {
    NodeIdList empty = {{0}, 0};
    if (!idx) return empty;
    for (int i = 0; i < idx->entry_count; i++) {
        if (strcmp(idx->entries[i].label, label) == 0)
            return idx->entries[i].nodes;
    }
    return empty;
}

EdgeIndex *index_create_edge(PropertyGraph *g) {
    EdgeIndex *idx = calloc(1, sizeof(EdgeIndex));
    if (!idx) return NULL;

    for (int i = 0; i < g->edge_count; i++) {
        Edge *edge = &g->edges[i];
        bool found = false;
        for (int e = 0; e < idx->entry_count; e++) {
            if (strcmp(idx->entries[e].type, edge->type) == 0) {
                EdgeIdList *list = &idx->entries[e].edges;
                if (list->count < MAX_INDEX_ENTRIES)
                    list->edge_ids[list->count++] = edge->id;
                found = true;
                break;
            }
        }
        if (!found && idx->entry_count < INDEX_BUCKETS) {
            TypeIndexEntry *entry = &idx->entries[idx->entry_count++];
            strncpy(entry->type, edge->type, MAX_EDGE_TYPE_LEN - 1);
            entry->type[MAX_EDGE_TYPE_LEN - 1] = '\0';
            entry->edges.edge_ids[0] = edge->id;
            entry->edges.count = 1;
        }
    }
    return idx;
}

EdgeIdList index_lookup_by_type(EdgeIndex *idx, const char *type) {
    EdgeIdList empty = {{0}, 0};
    if (!idx) return empty;
    for (int i = 0; i < idx->entry_count; i++) {
        if (strcmp(idx->entries[i].type, type) == 0)
            return idx->entries[i].edges;
    }
    return empty;
}

PropertyIndex *index_create_property(PropertyGraph *g, const char *key) {
    PropertyIndex *idx = calloc(1, sizeof(PropertyIndex));
    if (!idx) return NULL;

    int n = graph_node_count(g);
    for (int i = 0; i < n; i++) {
        Node *node = &g->nodes[i];
        const char *val = graph_node_get_property(node, key);
        if (!val) continue;

        bool found = false;
        for (int e = 0; e < idx->entry_count; e++) {
            PropertyIndexEntry *entry = &idx->entries[e];
            if (strcmp(entry->key, key) == 0 &&
                strcmp(entry->value, val) == 0) {
                if (entry->nodes.count < MAX_INDEX_ENTRIES)
                    entry->nodes.node_ids[entry->nodes.count++] = node->id;
                found = true;
                break;
            }
        }
        if (!found && idx->entry_count < INDEX_BUCKETS) {
            PropertyIndexEntry *entry = &idx->entries[idx->entry_count++];
            strncpy(entry->key, key, MAX_KEY_LEN - 1);
            entry->key[MAX_KEY_LEN - 1] = '\0';
            strncpy(entry->value, val, MAX_VALUE_LEN - 1);
            entry->value[MAX_VALUE_LEN - 1] = '\0';
            entry->nodes.node_ids[0] = node->id;
            entry->nodes.count = 1;
        }
    }
    return idx;
}

NodeIdList index_lookup_by_property(PropertyIndex *idx,
                                    const char *key, const char *value) {
    NodeIdList empty = {{0}, 0};
    if (!idx) return empty;
    for (int i = 0; i < idx->entry_count; i++) {
        PropertyIndexEntry *entry = &idx->entries[i];
        if (strcmp(entry->key, key) == 0 &&
            strcmp(entry->value, value) == 0)
            return entry->nodes;
    }
    return empty;
}

SpatialIndex *index_create_spatial(const BoundingBox *bounds) {
    SpatialIndex *si = calloc(1, sizeof(SpatialIndex));
    if (!si) return NULL;
    si->bounds = *bounds;
    si->cell_width = (bounds->max_x - bounds->min_x) / SPATIAL_GRID_SIZE;
    si->cell_height = (bounds->max_y - bounds->min_y) / SPATIAL_GRID_SIZE;
    return si;
}

bool index_spatial_insert(SpatialIndex *si, int64_t node_id,
                          double x, double y) {
    if (!si || si->node_count >= MAX_NODES) return false;

    int col = (int)((x - si->bounds.min_x) / si->cell_width);
    int row = (int)((y - si->bounds.min_y) / si->cell_height);

    if (col < 0) col = 0;
    if (col >= SPATIAL_GRID_SIZE) col = SPATIAL_GRID_SIZE - 1;
    if (row < 0) row = 0;
    if (row >= SPATIAL_GRID_SIZE) row = SPATIAL_GRID_SIZE - 1;

    NodeIdList *list = &si->grid[row][col];
    if (list->count < MAX_INDEX_ENTRIES) {
        list->node_ids[list->count++] = node_id;
        si->node_x[si->node_count] = x;
        si->node_y[si->node_count] = y;
        si->node_count++;
        return true;
    }
    return false;
}

NodeIdList index_spatial_range(SpatialIndex *si,
                               double min_x, double min_y,
                               double max_x, double max_y) {
    NodeIdList result = {{0}, 0};
    if (!si) return result;

    int col_start = (int)((min_x - si->bounds.min_x) / si->cell_width);
    int col_end = (int)((max_x - si->bounds.min_x) / si->cell_width);
    int row_start = (int)((min_y - si->bounds.min_y) / si->cell_height);
    int row_end = (int)((max_y - si->bounds.min_y) / si->cell_height);

    if (col_start < 0) col_start = 0;
    if (col_end >= SPATIAL_GRID_SIZE) col_end = SPATIAL_GRID_SIZE - 1;
    if (row_start < 0) row_start = 0;
    if (row_end >= SPATIAL_GRID_SIZE) row_end = SPATIAL_GRID_SIZE - 1;

    for (int r = row_start; r <= row_end; r++) {
        for (int c = col_start; c <= col_end; c++) {
            NodeIdList *list = &si->grid[r][c];
            for (int i = 0; i < list->count && result.count < MAX_INDEX_ENTRIES; i++)
                result.node_ids[result.count++] = list->node_ids[i];
        }
    }
    return result;
}

void index_destroy_node(NodeIndex *idx) { free(idx); }
void index_destroy_edge(EdgeIndex *idx) { free(idx); }
void index_destroy_property(PropertyIndex *idx) { free(idx); }
void index_destroy_spatial(SpatialIndex *si) { free(si); }
