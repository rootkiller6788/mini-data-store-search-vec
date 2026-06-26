#include "query_optimizer.h"
#include "row_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void demo_single_table_query(void) {
    printf("--- Single Table Query ---\n\n");

    OptCatalog cat;
    memset(&cat, 0, sizeof(cat));
    cat.num_tables = 1;
    strcpy(cat.catalog[0].table_name, "users");
    cat.catalog[0].stats.num_rows = 10000;
    cat.catalog[0].stats.num_pages = 1000;

    SQLStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.type = SQL_SELECT;
    strcpy(stmt.table, "users");
    stmt.num_columns = 2;
    strcpy(stmt.columns[0], "name");
    strcpy(stmt.columns[1], "age");
    stmt.has_where = 1;
    strcpy(stmt.where_clause.col.name, "age");
    stmt.where_clause.op = SQL_CMP_GT;
    stmt.where_clause.int_val = 25;
    stmt.has_order_by = 1;
    strcpy(stmt.order_by, "age");

    PlanNode *plan = opt_create_plan(&stmt, &cat);

    printf("Query: SELECT name,age FROM users WHERE age>25 ORDER BY age\n\n");
    printf("Estimated Plan:\n");
    opt_print_plan(plan, 1);
    printf("\nEstimated total cost: %.2f\n\n", plan->total_cost);

    opt_free_plan(plan);
}

static void demo_three_table_join(void) {
    printf("=== Three-Table Join Optimization (DP) ===\n\n");

    OptCatalog cat;
    memset(&cat, 0, sizeof(cat));
    cat.num_tables = 3;

    strcpy(cat.catalog[0].table_name, "users");
    cat.catalog[0].stats.num_rows = 1000;
    cat.catalog[0].stats.num_pages = 100;

    strcpy(cat.catalog[1].table_name, "orders");
    cat.catalog[1].stats.num_rows = 10000;
    cat.catalog[1].stats.num_pages = 500;

    strcpy(cat.catalog[2].table_name, "products");
    cat.catalog[2].stats.num_rows = 500;
    cat.catalog[2].stats.num_pages = 50;

    printf("Catalog:\n");
    for (int i = 0; i < cat.num_tables; i++) {
        printf("  %-12s  rows=%8.0f  pages=%6.0f\n",
               cat.catalog[i].table_name,
               cat.catalog[i].stats.num_rows,
               cat.catalog[i].stats.num_pages);
    }
    printf("\n");

    SQLStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.type = SQL_SELECT;
    strcpy(stmt.columns[0], "*");
    stmt.num_columns = 1;
    strcpy(stmt.table, "users");

    printf("--- Plan Shape: ((users JOIN orders) JOIN products) ---\n\n");

    PlanNode *users_scan = calloc(1, sizeof(PlanNode));
    users_scan->type = PLAN_SEQ_SCAN;
    strcpy(users_scan->table, "users");
    opt_estimate_cost(users_scan, &cat);

    PlanNode *orders_scan = calloc(1, sizeof(PlanNode));
    orders_scan->type = PLAN_SEQ_SCAN;
    strcpy(orders_scan->table, "orders");
    opt_estimate_cost(orders_scan, &cat);

    PlanNode *products_scan = calloc(1, sizeof(PlanNode));
    products_scan->type = PLAN_SEQ_SCAN;
    strcpy(products_scan->table, "products");
    opt_estimate_cost(products_scan, &cat);

    PlanNode *join1 = calloc(1, sizeof(PlanNode));
    join1->type = PLAN_HASH_JOIN;
    join1->left = users_scan;
    join1->right = orders_scan;
    opt_estimate_cost(join1, &cat);

    PlanNode *join2 = calloc(1, sizeof(PlanNode));
    join2->type = PLAN_HASH_JOIN;
    join2->left = join1;
    join2->right = products_scan;
    opt_estimate_cost(join2, &cat);

    printf("  Join order: (users JOIN orders) JOIN products\n");
    printf("  Total cost: %.2f\n\n", join2->total_cost);

    PlanNode *join1b = calloc(1, sizeof(PlanNode));
    join1b->type = PLAN_NESTED_LOOP_JOIN;
    join1b->left = users_scan;
    join1b->right = orders_scan;
    opt_estimate_cost(join1b, &cat);

    PlanNode *join2b = calloc(1, sizeof(PlanNode));
    join2b->type = PLAN_HASH_JOIN;
    join2b->left = join1b;
    join2b->right = products_scan;
    opt_estimate_cost(join2b, &cat);

    printf("  Join order: (users NLOOP orders) JOIN products\n");
    printf("  Total cost: %.2f\n\n", join2b->total_cost);

    printf("--- Plan Shape: (users JOIN (orders JOIN products)) ---\n\n");

    PlanNode *users_scan2 = calloc(1, sizeof(PlanNode));
    users_scan2->type = PLAN_SEQ_SCAN;
    strcpy(users_scan2->table, "users");
    opt_estimate_cost(users_scan2, &cat);

    PlanNode *orders_scan2 = calloc(1, sizeof(PlanNode));
    orders_scan2->type = PLAN_SEQ_SCAN;
    strcpy(orders_scan2->table, "orders");
    opt_estimate_cost(orders_scan2, &cat);

    PlanNode *products_scan2 = calloc(1, sizeof(PlanNode));
    products_scan2->type = PLAN_SEQ_SCAN;
    strcpy(products_scan2->table, "products");
    opt_estimate_cost(products_scan2, &cat);

    PlanNode *join_inner = calloc(1, sizeof(PlanNode));
    join_inner->type = PLAN_HASH_JOIN;
    join_inner->left = orders_scan2;
    join_inner->right = products_scan2;
    opt_estimate_cost(join_inner, &cat);

    PlanNode *join_outer = calloc(1, sizeof(PlanNode));
    join_outer->type = PLAN_HASH_JOIN;
    join_outer->left = users_scan2;
    join_outer->right = join_inner;
    opt_estimate_cost(join_outer, &cat);

    printf("  Join order: users JOIN (orders JOIN products)\n");
    printf("  Total cost: %.2f\n\n", join_outer->total_cost);

    PlanNode *join_inner2 = calloc(1, sizeof(PlanNode));
    join_inner2->type = PLAN_SORT_MERGE_JOIN;
    join_inner2->left = orders_scan2;
    join_inner2->right = products_scan2;
    opt_estimate_cost(join_inner2, &cat);

    PlanNode *join_outer2 = calloc(1, sizeof(PlanNode));
    join_outer2->type = PLAN_HASH_JOIN;
    join_outer2->left = users_scan2;
    join_outer2->right = join_inner2;
    opt_estimate_cost(join_outer2, &cat);

    printf("  Join order: users JOIN (orders SMERGE products)\n");
    printf("  Total cost: %.2f\n\n", join_outer2->total_cost);

    printf("--- DP Best Plan ---\n\n");

    double costs[4] = { join2->total_cost, join2b->total_cost,
                         join_outer->total_cost, join_outer2->total_cost };
    const char *names[4] = {
        "(u HJ o) HJ p",
        "(u NL o) HJ p",
        "u HJ (o HJ p)",
        "u HJ (o SM p)"
    };
    int best = 0;
    for (int i = 1; i < 4; i++) {
        if (costs[i] < costs[best]) best = i;
    }

    printf("DP selected: %s (cost=%.2f)\n", names[best], costs[best]);
    printf("DP would choose this order as optimal.\n\n");

    printf("=== Cost Model ===\n\n");
    printf("SeqScan  = pages * 0.1\n");
    printf("Filter   = selectivity * rows\n");
    printf("HashJoin = (outer_rows + inner_rows) * 0.5\n");
    printf("NLOOP    = outer_rows * inner_rows * 0.05\n");
    printf("SrtMerge = sort(outer) + sort(inner) + merge_scan\n\n");

    opt_free_plan(join2);
    opt_free_plan(join2b);
    opt_free_plan(join_outer);
    opt_free_plan(join_outer2);
}

int main(void) {
    printf("=== Mini Relational DB - Query Optimizer Demo ===\n\n");
    demo_single_table_query();
    demo_three_table_join();
    printf("=== Done ===\n");
    return 0;
}
