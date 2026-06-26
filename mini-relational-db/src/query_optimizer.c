#include "query_optimizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static PlanNode *plan_alloc(PlanNodeType type) {
    PlanNode *n = calloc(1, sizeof(PlanNode));
    if (!n) { fprintf(stderr, "plan_alloc: OOM\n"); exit(1); }
    n->type = type;
    n->startup_cost = 0.0;
    n->total_cost   = 0.0;
    n->rows         = 1000.0;
    n->width        = 64.0;
    return n;
}

static void catalog_init(OptCatalog *cat) {
    if (!cat) return;
    for (int i = 0; i < OPT_MAX_TABLES; i++) {
        cat->catalog[i].stats.num_rows  = 1000.0;
        cat->catalog[i].stats.num_pages = 100.0;
    }
}

static const CatalogEntry *catalog_find(const OptCatalog *cat, const char *name) {
    for (int i = 0; i < cat->num_tables; i++)
        if (strcmp(cat->catalog[i].table_name, name) == 0)
            return &cat->catalog[i];
    return NULL;
}

static double estimate_seq_scan_cost(double num_pages, double num_rows) {
    (void)num_rows;
    return num_pages * 0.1;
}

static double estimate_filter_selectivity(const WhereClause *wc) {
    if (!wc) return 1.0;
    switch (wc->op) {
        case SQL_CMP_EQ: return 0.01;
        case SQL_CMP_NE: return 0.99;
        case SQL_CMP_LT:
        case SQL_CMP_LE:
        case SQL_CMP_GT:
        case SQL_CMP_GE: return 0.33;
        default:          return 1.0;
    }
}

static double estimate_sort_cost(double num_rows) {
    if (num_rows <= 1.0) return 0.0;
    return num_rows * log2(num_rows > 0.0 ? num_rows : 2.0) * 0.2;
}

static double estimate_join_cost(double outer_rows, double inner_rows, PlanNodeType jtype) {
    double cost = 0.0;
    switch (jtype) {
        case PLAN_NESTED_LOOP_JOIN:
            cost = outer_rows * inner_rows * 0.05;
            break;
        case PLAN_HASH_JOIN:
            cost = (outer_rows + inner_rows) * 0.5;
            break;
        case PLAN_SORT_MERGE_JOIN:
            cost = estimate_sort_cost(outer_rows) + estimate_sort_cost(inner_rows)
                 + (outer_rows + inner_rows) * 0.2;
            break;
        default:
            cost = outer_rows * inner_rows;
            break;
    }
    return cost;
}

static double estimate_plan_rows(PlanNode *node, const OptCatalog *cat) {
    if (!node) return 1.0;
    switch (node->type) {
        case PLAN_SEQ_SCAN: {
            const CatalogEntry *ce = catalog_find(cat, node->table);
            return ce ? ce->stats.num_rows : 1000.0;
        }
        case PLAN_INDEX_SCAN:
            return 10.0;
        case PLAN_FILTER: {
            double child_rows = estimate_plan_rows(node->left, cat);
            double sel = estimate_filter_selectivity(&node->where_clause);
            return child_rows * sel;
        }
        case PLAN_PROJECTION:
            return estimate_plan_rows(node->left, cat);
        case PLAN_SORT:
            return estimate_plan_rows(node->left, cat);
        case PLAN_HASH_AGG:
            return estimate_plan_rows(node->left, cat) * 0.2;
        default:
            return 1000.0;
    }
}

void opt_estimate_cost(PlanNode *plan, const OptCatalog *catalog) {
    if (!plan) return;

    opt_estimate_cost(plan->left,  catalog);
    opt_estimate_cost(plan->right, catalog);

    double lrows = plan->left  ? plan->left->rows  : 1.0;
    double rrows = plan->right ? plan->right->rows : 1.0;

    switch (plan->type) {
    case PLAN_SEQ_SCAN: {
        const CatalogEntry *ce = catalog_find(catalog, plan->table);
        double pages = ce ? ce->stats.num_pages : 100.0;
        double rows  = ce ? ce->stats.num_rows  : 1000.0;
        plan->startup_cost = 0.0;
        plan->total_cost   = estimate_seq_scan_cost(pages, rows);
        plan->rows         = rows;
        plan->width        = 64.0;
        break;
    }
    case PLAN_INDEX_SCAN:
        plan->startup_cost = 1.0;
        plan->total_cost   = 5.0;
        plan->rows         = 10.0;
        plan->width        = 64.0;
        break;
    case PLAN_FILTER:
        plan->startup_cost = plan->left ? plan->left->startup_cost : 0.0;
        plan->total_cost   = (plan->left ? plan->left->total_cost : 0.0) + plan->left->rows * 0.01;
        plan->rows         = plan->left->rows * estimate_filter_selectivity(&plan->where_clause);
        plan->width        = plan->left ? plan->left->width : 64.0;
        break;
    case PLAN_PROJECTION:
        plan->startup_cost = plan->left ? plan->left->startup_cost : 0.0;
        plan->total_cost   = (plan->left ? plan->left->total_cost : 0.0) + plan->left->rows * 0.01;
        plan->rows         = plan->left ? plan->left->rows : 0.0;
        plan->width        = plan->num_columns * 8.0;
        break;
    case PLAN_SORT:
        plan->startup_cost = plan->left ? plan->left->total_cost : 0.0;
        plan->total_cost   = (plan->left ? plan->left->total_cost : 0.0) + estimate_sort_cost(lrows);
        plan->rows         = plan->left ? plan->left->rows : 0.0;
        plan->width        = plan->left ? plan->left->width : 64.0;
        break;
    case PLAN_HASH_JOIN:
    case PLAN_NESTED_LOOP_JOIN:
    case PLAN_SORT_MERGE_JOIN:
        plan->startup_cost = (plan->left ? plan->left->total_cost : 0.0)
                           + (plan->right ? plan->right->total_cost : 0.0);
        plan->total_cost   = plan->startup_cost + estimate_join_cost(lrows, rrows, plan->type);
        plan->rows         = lrows * rrows * 0.1;
        if (plan->rows < 1.0) plan->rows = 1.0;
        plan->width        = (plan->left ? plan->left->width : 64.0)
                           + (plan->right ? plan->right->width : 64.0);
        break;
    case PLAN_HASH_AGG:
        plan->startup_cost = plan->left ? plan->left->total_cost : 0.0;
        plan->total_cost   = plan->startup_cost + lrows * 0.15;
        plan->rows         = lrows * 0.2;
        plan->width        = 32.0;
        break;
    default:
        break;
    }
}

PlanNode *opt_create_plan(const SQLStmt *stmt, const OptCatalog *catalog) {
    if (!stmt || stmt->type != SQL_SELECT) return NULL;

    if (strcmp(stmt->columns[0], "*") == 0 && stmt->num_columns == 1) {
        PlanNode *scan = plan_alloc(PLAN_SEQ_SCAN);
        strcpy(scan->table, stmt->table);

        PlanNode *root = scan;

        if (stmt->has_where) {
            PlanNode *filt = plan_alloc(PLAN_FILTER);
            filt->left = scan;
            filt->has_where = 1;
            filt->where_clause = stmt->where_clause;
            root = filt;
        }

        if (stmt->has_order_by) {
            PlanNode *sort = plan_alloc(PLAN_SORT);
            sort->left = root;
            sort->has_order_by = 1;
            strcpy(sort->order_by, stmt->order_by);
            root = sort;
        }

        opt_estimate_cost(root, catalog);
        return root;
    }

    PlanNode *scan = plan_alloc(PLAN_SEQ_SCAN);
    strcpy(scan->table, stmt->table);

    PlanNode *proj = plan_alloc(PLAN_PROJECTION);
    proj->left = scan;
    proj->num_columns = stmt->num_columns;
    for (int i = 0; i < stmt->num_columns; i++)
        strcpy(proj->columns[i], stmt->columns[i]);

    PlanNode *root = proj;

    if (stmt->has_where) {
        PlanNode *filt = plan_alloc(PLAN_FILTER);
        filt->left = root;
        filt->has_where = 1;
        filt->where_clause = stmt->where_clause;
        root = filt;
    }

    if (stmt->has_order_by) {
        PlanNode *sort = plan_alloc(PLAN_SORT);
        sort->left = root;
        sort->has_order_by = 1;
        strcpy(sort->order_by, stmt->order_by);
        root = sort;
    }

    opt_estimate_cost(root, catalog);
    return root;
}

typedef struct {
    int    table_ids[OPT_MAX_TABLES];
    int    num_tables;
    int    used[OPT_MAX_TABLES];
    PlanNode *best_plan;
    double    best_cost;
} DPState;

static void dp_enumerate(DPState *dp, const OptCatalog *catalog, int depth,
                          PlanNode *current, const int all_ids[], int num_all) {
    if (dp->num_tables == 1 && !current) {
        int tid = dp->table_ids[0];
        PlanNode *scan = plan_alloc(PLAN_SEQ_SCAN);
        strcpy(scan->table, catalog->catalog[tid].table_name);
        strcpy(scan->tables[0], catalog->catalog[tid].table_name);
        scan->num_tables = 1;
        opt_estimate_cost(scan, catalog);
        dp->best_plan = scan;
        dp->best_cost = scan->total_cost;
        return;
    }

    if (current && dp->num_tables == num_all) {
        opt_estimate_cost(current, catalog);
        if (current->total_cost < dp->best_cost || !dp->best_plan) {
            if (dp->best_plan) opt_free_plan(dp->best_plan);
            dp->best_plan = current;
            dp->best_cost = current->total_cost;
        } else {
            opt_free_plan(current);
        }
        return;
    }

    if (!current) {
        for (int i = 0; i < num_all; i++) {
            if (!dp->used[i]) {
                dp->used[i] = 1;
                dp->table_ids[dp->num_tables++] = i;
                dp_enumerate(dp, catalog, depth + 1, NULL, all_ids, num_all);
                dp->num_tables--;
                dp->used[i] = 0;
            }
        }
    } else {
        for (int i = 0; i < num_all; i++) {
            if (!dp->used[i]) {
                dp->used[i] = 1;
                dp->table_ids[dp->num_tables++] = i;

                PlanNode *scan = plan_alloc(PLAN_SEQ_SCAN);
                strcpy(scan->table, catalog->catalog[i].table_name);
                strcpy(scan->tables[0], catalog->catalog[i].table_name);
                scan->num_tables = 1;

                PlanNodeType join_types[] = { PLAN_HASH_JOIN, PLAN_NESTED_LOOP_JOIN, PLAN_SORT_MERGE_JOIN };
                for (int j = 0; j < 3; j++) {
                    PlanNode *join = plan_alloc(join_types[j]);
                    join->left = current;
                    join->right = scan;
                    /* Deep copy current to avoid double-free in recursion */
                    PlanNode *saved = plan_alloc(plan_alloc(PLAN_SEQ_SCAN)->type);
                    *saved = *current; /* shallow copy; children point to same nodes */
                    saved->left  = current->left;
                    saved->right = current->right;

                    join->left = saved;
                    PlanNode *join_copy = plan_alloc(join->type);
                    *join_copy = *join;
                    join_copy->left = saved;
                    join_copy->right = plan_alloc(PLAN_SEQ_SCAN);
                    *join_copy->right = *scan;

                    opt_estimate_cost(join_copy, catalog);
                    if (dp->num_tables == num_all) {
                        if (join_copy->total_cost < dp->best_cost || !dp->best_plan) {
                            if (dp->best_plan) opt_free_plan(dp->best_plan);
                            dp->best_plan = join_copy;
                            dp->best_cost = join_copy->total_cost;
                        } else {
                            opt_free_plan(join_copy);
                        }
                    }
                    /* Simpler: just try each shape and keep best */
                    free(join_copy->right);
                    free(join_copy);
                    free(join);
                    join_copy = NULL;
                }
                free(scan);

                dp->num_tables--;
                dp->used[i] = 0;
            }
        }
    }
}

PlanNode *opt_choose_best(const SQLStmt *stmt, const OptCatalog *catalog) {
    if (!stmt || !catalog || catalog->num_tables < 2) {
        return opt_create_plan(stmt, catalog);
    }

    DPState dp;
    memset(&dp, 0, sizeof(dp));
    dp.best_cost = 1e100;

    int all_ids[OPT_MAX_TABLES];
    for (int i = 0; i < catalog->num_tables; i++)
        all_ids[i] = i;

    dp_enumerate(&dp, catalog, 0, NULL, all_ids, catalog->num_tables);

    if (dp.best_plan) {
        return dp.best_plan;
    }
    return opt_create_plan(stmt, catalog);
}

static void opt_print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

void opt_print_plan(const PlanNode *plan, int indent) {
    if (!plan) return;

    opt_print_indent(indent);

    switch (plan->type) {
        case PLAN_SEQ_SCAN:
            printf("SeqScan on %s (cost=%.2f..%.2f rows=%.0f width=%.0f)\n",
                   plan->table, plan->startup_cost, plan->total_cost,
                   plan->rows, plan->width);
            break;
        case PLAN_INDEX_SCAN:
            printf("IndexScan on %s (cost=%.2f..%.2f rows=%.0f)\n",
                   plan->table, plan->startup_cost, plan->total_cost, plan->rows);
            break;
        case PLAN_HASH_JOIN:
            printf("HashJoin (cost=%.2f..%.2f rows=%.0f width=%.0f)\n",
                   plan->startup_cost, plan->total_cost, plan->rows, plan->width);
            break;
        case PLAN_NESTED_LOOP_JOIN:
            printf("NestedLoopJoin (cost=%.2f..%.2f rows=%.0f width=%.0f)\n",
                   plan->startup_cost, plan->total_cost, plan->rows, plan->width);
            break;
        case PLAN_SORT_MERGE_JOIN:
            printf("SortMergeJoin (cost=%.2f..%.2f rows=%.0f width=%.0f)\n",
                   plan->startup_cost, plan->total_cost, plan->rows, plan->width);
            break;
        case PLAN_FILTER:
            printf("Filter (cost=%.2f..%.2f rows=%.0f)\n",
                   plan->startup_cost, plan->total_cost, plan->rows);
            break;
        case PLAN_PROJECTION:
            printf("Projection [");
            for (int i = 0; i < plan->num_columns; i++) {
                if (i > 0) printf(",");
                printf("%s", plan->columns[i]);
            }
            printf("] (cost=%.2f..%.2f rows=%.0f)\n",
                   plan->startup_cost, plan->total_cost, plan->rows);
            break;
        case PLAN_SORT:
            printf("Sort by %s (cost=%.2f..%.2f rows=%.0f)\n",
                   plan->order_by, plan->startup_cost, plan->total_cost, plan->rows);
            break;
        case PLAN_HASH_AGG:
            printf("HashAgg (cost=%.2f..%.2f rows=%.0f)\n",
                   plan->startup_cost, plan->total_cost, plan->rows);
            break;
        default:
            printf("Unknown plan node\n");
            break;
    }

    if (plan->left)  opt_print_plan(plan->left,  indent + 1);
    if (plan->right) opt_print_plan(plan->right, indent + 1);
}

void opt_free_plan(PlanNode *plan) {
    if (!plan) return;
    opt_free_plan(plan->left);
    opt_free_plan(plan->right);
    free(plan);
}
