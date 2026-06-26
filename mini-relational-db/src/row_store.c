#include "row_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Table *table_create(const char *name, int num_cols, const ColumnDef *defs) {
    Table *t = calloc(1, sizeof(Table));
    if (!t) { fprintf(stderr, "table_create: OOM\n"); exit(1); }

    strncpy(t->name, name, TABLE_MAX_NAME - 1);
    t->name[TABLE_MAX_NAME - 1] = '\0';
    t->num_columns = num_cols;
    t->num_rows = 0;
    t->next_rowid = 0;

    if (defs) {
        for (int i = 0; i < num_cols && i < TABLE_MAX_COLS; i++) {
            t->columns[i].name[0] = '\0';
            if (defs[i].name[0]) {
                strncpy(t->columns[i].name, defs[i].name, ROW_MAX_VALUE - 1);
                t->columns[i].name[ROW_MAX_VALUE - 1] = '\0';
            }
            t->columns[i].type = defs[i].type;
            t->columns[i].length = defs[i].length;
            t->columns[i].is_nullable = 1;

            t->schema.columns[i] = t->columns[i];
        }
        t->schema.num_columns = num_cols;
    }
    return t;
}

int table_insert(Table *table, int num_vals, const char *vals[]) {
    if (!table || num_vals <= 0) return -1;
    if (table->num_rows >= TABLE_MAX_ROWS) return -1;

    Row *r = &table->rows[table->num_rows];
    memset(r, 0, sizeof(Row));
    r->num_fields = num_vals < ROW_MAX_COLUMNS ? num_vals : ROW_MAX_COLUMNS;
    r->is_deleted = 0;

    for (int i = 0; i < r->num_fields; i++) {
        if (vals && vals[i]) {
            strncpy(r->values[i], vals[i], ROW_MAX_VALUE - 1);
            r->values[i][ROW_MAX_VALUE - 1] = '\0';
        } else {
            r->values[i][0] = '\0';
        }
    }

    table->num_rows++;
    return table->num_rows - 1;
}

int table_scan(const Table *table, int start_rowid, Row *out) {
    if (!table || !out) return -1;

    for (int i = start_rowid; i < table->num_rows; i++) {
        if (!table->rows[i].is_deleted) {
            *out = table->rows[i];
            return i + 1;
        }
    }
    return -1;
}

int table_find_col(const Table *table, const char *col_name) {
    if (!table || !col_name) return -1;
    for (int i = 0; i < table->num_columns; i++) {
        if (strcmp(table->columns[i].name, col_name) == 0)
            return i;
    }
    return -1;
}

int table_get_col_index(const Table *table, const char *col_name) {
    return table_find_col(table, col_name);
}

int table_compare_rows(const Row *a, const Row *b, int col_idx) {
    if (!a || !b || col_idx < 0) return 0;
    return strcmp(a->values[col_idx], b->values[col_idx]);
}

int table_sort(Table *table, int col_idx) {
    if (!table || col_idx < 0 || col_idx >= table->num_columns) return -1;

    for (int i = 0; i < table->num_rows - 1; i++) {
        for (int j = i + 1; j < table->num_rows; j++) {
            if (table_compare_rows(&table->rows[i], &table->rows[j], col_idx) > 0) {
                Row tmp = table->rows[i];
                table->rows[i] = table->rows[j];
                table->rows[j] = tmp;
            }
        }
    }
    return 0;
}

void table_print(const Table *table) {
    if (!table) { printf("(null)\n"); return; }

    printf("=== %s (%d columns, %d rows) ===\n",
           table->name, table->num_columns, table->num_rows);

    if (table->num_columns > 0 && table->num_rows > 0) {
        for (int c = 0; c < table->num_columns; c++) {
            if (c > 0) printf(" | ");
            printf("%-12s", table->columns[c].name[0] ? table->columns[c].name : "?");
        }
        printf("\n");
        for (int c = 0; c < table->num_columns; c++) {
            if (c > 0) printf("-+-");
            printf("------------");
        }
        printf("\n");

        for (int r = 0; r < table->num_rows; r++) {
            if (table->rows[r].is_deleted) continue;
            for (int c = 0; c < table->num_columns; c++) {
                if (c > 0) printf(" | ");
                printf("%-12s", table->rows[r].values[c]);
            }
            printf("\n");
        }
    }
    printf("\n");
}

void table_free(Table *table) {
    free(table);
}
