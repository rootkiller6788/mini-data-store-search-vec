#ifndef ROW_STORE_H
#define ROW_STORE_H

#include "sql_parser.h"
#include <stddef.h>
#include <stdint.h>

#define ROW_MAX_COLUMNS 16
#define ROW_MAX_VALUE   64
#define TABLE_MAX_ROWS  1024
#define TABLE_MAX_NAME  64
#define TABLE_MAX_COLS  16

typedef struct {
    char        name[ROW_MAX_VALUE];
    SQLDataType type;
    int         length;
    int         is_nullable;
} Column;

typedef struct {
    int     num_columns;
    Column  columns[TABLE_MAX_COLS];
} Schema;

typedef struct {
    char values[ROW_MAX_COLUMNS][ROW_MAX_VALUE];
    int  num_fields;
    int  is_deleted;
} Row;

typedef struct {
    char    name[TABLE_MAX_NAME];
    int     num_columns;
    Column  columns[TABLE_MAX_COLS];
    Schema  schema;
    int     num_rows;
    Row     rows[TABLE_MAX_ROWS];
    int     next_rowid;
} Table;

Table  *table_create(const char *name, int num_cols, const ColumnDef *defs);
int     table_insert(Table *table, int num_vals, const char *vals[]);
int     table_scan(const Table *table, int start_rowid, Row *out);
int     table_find_col(const Table *table, const char *col_name);
void    table_print(const Table *table);
void    table_free(Table *table);
int     table_get_col_index(const Table *table, const char *col_name);
int     table_compare_rows(const Row *a, const Row *b, int col_idx);
int     table_sort(Table *table, int col_idx);

#endif
