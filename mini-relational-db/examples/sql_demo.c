#include "sql_parser.h"
#include "row_store.h"
#include "volcano_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== Mini Relational DB - SQL Demo ===\n\n");

    printf("--- Parser Tests ---\n\n");

    SQLStmt stmt;

    if (sql_parse_select("SELECT name,age FROM users WHERE age>20 ORDER BY age", &stmt)) {
        printf("Parsed SELECT: ");
        sql_print_stmt(&stmt);
        printf("\n");
    } else {
        printf("SELECT parse failed\n");
    }

    if (sql_parse_create("CREATE TABLE users (id INT, name VARCHAR(50), age INT)", &stmt)) {
        printf("Parsed DDL: ");
        sql_print_stmt(&stmt);
        printf("\n");
    } else {
        printf("CREATE parse failed\n");
    }

    if (sql_parse_insert("INSERT INTO users VALUES (1, 'Alice', 30)", &stmt)) {
        printf("Parsed DML: ");
        sql_print_stmt(&stmt);
        printf("\n");
    } else {
        printf("INSERT parse failed\n");
    }

    if (sql_parse_drop("DROP TABLE users", &stmt)) {
        printf("Parsed DROP: ");
        sql_print_stmt(&stmt);
        printf("\n");
    } else {
        printf("DROP parse failed\n");
    }

    printf("\n--- Table Operations ---\n\n");

    ColumnDef defs[] = {
        {"id",   SQL_TYPE_INT,     0},
        {"name", SQL_TYPE_VARCHAR, 50},
        {"age",  SQL_TYPE_INT,     0},
    };
    Table *users = table_create("users", 3, defs);

    const char *r1[] = {"1", "Alice",  "30"};
    const char *r2[] = {"2", "Bob",    "25"};
    const char *r3[] = {"3", "Carol",  "35"};
    const char *r4[] = {"4", "Dave",   "22"};

    table_insert(users, 3, r1);
    table_insert(users, 3, r2);
    table_insert(users, 3, r3);
    table_insert(users, 3, r4);

    printf("Users table:\n");
    table_print(users);

    printf("--- Volcano Executor Pipeline ---\n\n");

    Executor *scan  = exec_create_seq_scan(users);

    WhereClause wc;
    memset(&wc, 0, sizeof(wc));
    strcpy(wc.col.name, "age");
    wc.op = SQL_CMP_GT;
    wc.int_val = 22;

    Executor *filt  = exec_create_filter(scan, &wc);

    char proj_cols[2][SQL_MAX_NAME];
    strcpy(proj_cols[0], "name");
    strcpy(proj_cols[1], "age");

    Executor *proj  = exec_create_project(filt, 2, proj_cols);

    Executor *sort  = exec_create_sort(proj, "age");

    printf("SELECT name,age FROM users WHERE age>22 ORDER BY age:\n");
    exec_open(sort);
    for (int i = 0; i < 20; i++) {
        Tuple t = exec_next(sort);
        if (t.eof) break;
        printf("  ");
        for (int f = 0; f < t.num_fields; f++) {
            if (f > 0) printf(" | ");
            printf("%s", t.fields[f]);
        }
        printf("\n");
    }
    exec_close(sort);

    exec_free(sort);
    exec_free(proj);
    exec_free(filt);
    exec_free(scan);
    table_free(users);

    printf("\n=== Done ===\n");
    return 0;
}
