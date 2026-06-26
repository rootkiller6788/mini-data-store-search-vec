#include "row_store.h"
#include "join_algorithms.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== Mini Relational DB - Join Algorithms Demo ===\n\n");

    /* Build users table */
    ColumnDef user_defs[] = {
        {"uid",   SQL_TYPE_INT,     0},
        {"name",  SQL_TYPE_VARCHAR, 50},
        {"email", SQL_TYPE_VARCHAR, 100},
    };
    Table *users = table_create("users", 3, user_defs);

    const char *u1[] = {"1", "Alice", "alice@example.com"};
    const char *u2[] = {"2", "Bob",   "bob@example.com"};
    const char *u3[] = {"3", "Carol", "carol@example.com"};
    const char *u4[] = {"4", "Dave",  "dave@example.com"};
    const char *u5[] = {"5", "Eve",   "eve@example.com"};

    table_insert(users, 3, u1);
    table_insert(users, 3, u2);
    table_insert(users, 3, u3);
    table_insert(users, 3, u4);
    table_insert(users, 3, u5);

    printf("Table 'users':\n");
    table_print(users);

    ColumnDef order_defs[] = {
        {"oid",    SQL_TYPE_INT,     0},
        {"uid",    SQL_TYPE_INT,     0},
        {"amount", SQL_TYPE_INT,     0},
    };
    Table *orders = table_create("orders", 3, order_defs);

    const char *o1[] = {"101", "1", "99"};
    const char *o2[] = {"102", "2", "150"};
    const char *o3[] = {"103", "1", "200"};
    const char *o4[] = {"104", "3", "75"};
    const char *o5[] = {"105", "2", "300"};
    const char *o6[] = {"106", "5", "50"};
    const char *o7[] = {"107", "1", "125"};

    table_insert(orders, 3, o1);
    table_insert(orders, 3, o2);
    table_insert(orders, 3, o3);
    table_insert(orders, 3, o4);
    table_insert(orders, 3, o5);
    table_insert(orders, 3, o6);
    table_insert(orders, 3, o7);

    printf("Table 'orders':\n");
    table_print(orders);

    printf("\n=== Join: users JOIN orders ON uid = uid ===\n\n");

    JoinComparison comparisons[4];
    join_compare_all(users, orders, "uid", "uid", comparisons);

    for (int i = 0; i < 4; i++) {
        join_print_comparison(&comparisons[i]);
        printf("\n");
    }

    printf("--- Detailed Join Results ---\n\n");

    HashTable ht;
    join_hash_build(&ht, orders, "uid");

    Table *result = table_create("join_result", 0, NULL);
    JoinComparison cmp;
    int n = join_hash_probe(&ht, users, "uid", result, &cmp);
    printf("Hash Join produced %d tuples:\n", n);
    table_print(result);
    hash_table_destroy(&ht);
    table_free(result);

    result = table_create("join_result", 0, NULL);
    n = join_nested_loop(users, orders, "uid", "uid", result, &cmp);
    printf("Nested Loop Join produced %d tuples:\n", n);
    table_print(result);
    table_free(result);

    result = table_create("join_result", 0, NULL);
    n = join_sort_merge(users, orders, "uid", "uid", result, &cmp);
    printf("Sort-Merge Join produced %d tuples:\n", n);
    table_print(result);
    table_free(result);

    printf("=== Efficiency Summary ===\n");
    printf("Method           Compares  IO_Pages\n");
    printf("-----------------------------------\n");
    for (int i = 0; i < 3; i++) {
        const char *names[] = {"Hash       ", "Nested-Loop", "Sort-Merge "};
        printf("%s  %.0f       %.2f\n",
               names[i], comparisons[i].compare_ops, comparisons[i].io_pages);
    }

    table_free(users);
    table_free(orders);

    printf("\n=== Done ===\n");
    return 0;
}
