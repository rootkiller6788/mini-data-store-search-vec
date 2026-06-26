#ifndef QUERY_OPTIMIZER_H
#define QUERY_OPTIMIZER_H

#include "sql_parser.h"
#include <stddef.h>

typedef enum {
    PLAN_SEQ_SCAN       = 0,
    PLAN_INDEX_SCAN     = 1,
    PLAN_HASH_JOIN      = 2,
    PLAN_NESTED_LOOP_JOIN = 3,
    PLAN_SORT_MERGE_JOIN = 4,
    PLAN_FILTER         = 5,
    PLAN_PROJECTION     = 6,
    PLAN_SORT           = 7,
    PLAN_HASH_AGG       = 8
} PlanNodeType;

typedef struct PlanNode PlanNode;

struct PlanNode {
    PlanNodeType type;
    PlanNode    *left;
    PlanNode    *right;

    char  table[SQL_MAX_NAME];
    int   num_tables;
    char  tables[SQL_MAX_COLUMNS][SQL_MAX_NAME];

    char  columns[SQL_MAX_COLUMNS][SQL_MAX_NAME];
    int   num_columns;

    WhereClause where_clause;
    int   has_where;
    int   has_order_by;
    char  order_by[SQL_MAX_NAME];

    double startup_cost;
    double total_cost;
    double rows;
    double width;
};

typedef struct {
    double startup_cost;
    double total_cost;
    double rows;
    double width;
} CostEstimate;

typedef struct {
    double num_rows;
    double num_pages;
} TableStats;

typedef struct {
    char        table_name[SQL_MAX_NAME];
    TableStats  stats;
} CatalogEntry;

#define OPT_MAX_TABLES 8

typedef struct {
    int           num_tables;
    CatalogEntry  catalog[OPT_MAX_TABLES];
} OptCatalog;

PlanNode  *opt_create_plan(const SQLStmt *stmt, const OptCatalog *catalog);
void       opt_estimate_cost(PlanNode *plan, const OptCatalog *catalog);
PlanNode  *opt_choose_best(const SQLStmt *stmt, const OptCatalog *catalog);
void       opt_print_plan(const PlanNode *plan, int indent);
void       opt_free_plan(PlanNode *plan);

#endif
