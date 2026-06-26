#ifndef GRAPH_INDEX_H
#define GRAPH_INDEX_H

#include "property_graph.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_INDEX_ENTRIES  4096
#define SPATIAL_GRID_SIZE  16
#define INDEX_BUCKETS      256

typedef struct {
    int64_t node_ids[MAX_INDEX_ENTRIES];
    int count;
} NodeIdList;

typedef struct {
    int64_t edge_ids[MAX_INDEX_ENTRIES];
    int count;
} EdgeIdList;

typedef struct {
    char label[MAX_LABEL_LEN];
    NodeIdList nodes;
} LabelIndexEntry;

typedef struct {
    LabelIndexEntry entries[INDEX_BUCKETS];
    int entry_count;
} NodeIndex;

typedef struct {
    char type[MAX_EDGE_TYPE_LEN];
    EdgeIdList edges;
} TypeIndexEntry;

typedef struct {
    TypeIndexEntry entries[INDEX_BUCKETS];
    int entry_count;
} EdgeIndex;

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
    NodeIdList nodes;
} PropertyIndexEntry;

typedef struct {
    PropertyIndexEntry entries[INDEX_BUCKETS];
    int entry_count;
} PropertyIndex;

typedef struct {
    double min_x, min_y, max_x, max_y;
} BoundingBox;

typedef struct {
    BoundingBox bounds;
    double cell_width;
    double cell_height;
    NodeIdList grid[SPATIAL_GRID_SIZE][SPATIAL_GRID_SIZE];
    double node_x[MAX_NODES];
    double node_y[MAX_NODES];
    int node_count;
} SpatialIndex;

NodeIndex *index_create_node(PropertyGraph *g);
NodeIdList index_lookup_by_label(NodeIndex *idx, const char *label);

EdgeIndex *index_create_edge(PropertyGraph *g);
EdgeIdList index_lookup_by_type(EdgeIndex *idx, const char *type);

PropertyIndex *index_create_property(PropertyGraph *g, const char *key);
NodeIdList index_lookup_by_property(PropertyIndex *idx,
                                    const char *key, const char *value);

SpatialIndex *index_create_spatial(const BoundingBox *bounds);
bool index_spatial_insert(SpatialIndex *si, int64_t node_id,
                          double x, double y);
NodeIdList index_spatial_range(SpatialIndex *si,
                               double min_x, double min_y,
                               double max_x, double max_y);

void index_destroy_node(NodeIndex *idx);
void index_destroy_edge(EdgeIndex *idx);
void index_destroy_property(PropertyIndex *idx);
void index_destroy_spatial(SpatialIndex *si);

#endif
