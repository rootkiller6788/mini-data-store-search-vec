#ifndef CYPHER_LIKE_H
#define CYPHER_LIKE_H

#include "property_graph.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_NODE_MATCHES 4
#define MAX_EDGE_MATCHES 4
#define MAX_VARIABLES    8
#define MAX_RESULT_ROWS  256

typedef enum {
    DIR_LEFT,
    DIR_RIGHT,
    DIR_EITHER
} EdgeDirection;

typedef struct {
    char variable[MAX_LABEL_LEN];
    char labels[MAX_NODE_LABELS][MAX_LABEL_LEN];
    int label_count;
    Property property_filters[MAX_NODE_PROPERTIES];
    int filter_count;
} NodeMatch;

typedef struct {
    char variable[MAX_LABEL_LEN];
    char type[MAX_EDGE_TYPE_LEN];
    EdgeDirection direction;
} EdgeMatch;

typedef struct {
    NodeMatch node_matches[MAX_NODE_MATCHES];
    int node_match_count;
    EdgeMatch edge_matches[MAX_EDGE_MATCHES];
    int edge_match_count;
} QueryPattern;

typedef struct {
    char var_name[MAX_LABEL_LEN];
    int64_t node_id;
} VariableBinding;

typedef struct {
    VariableBinding bindings[MAX_VARIABLES];
    int binding_count;
} QueryRow;

typedef struct {
    QueryRow rows[MAX_RESULT_ROWS];
    int row_count;
    bool success;
    char error_message[256];
} QueryResult;

bool cypher_parse(const char *query_string, QueryPattern *pattern);
QueryResult cypher_match(PropertyGraph *g, QueryPattern *pattern);
void cypher_print_results(QueryResult *result);
void cypher_pattern_print(QueryPattern *pattern);

#endif
