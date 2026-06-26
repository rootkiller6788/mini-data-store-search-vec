#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include <stddef.h>

#define SQL_MAX_COLUMNS  16
#define SQL_MAX_NAME     64
#define SQL_MAX_WHERE    256

typedef enum {
    SQL_TYPE_INT     = 0,
    SQL_TYPE_VARCHAR = 1,
    SQL_TYPE_TEXT    = 2
} SQLDataType;

typedef enum {
    SQL_SELECT      = 0,
    SQL_INSERT      = 1,
    SQL_CREATE_TABLE = 2,
    SQL_DROP_TABLE  = 3,
    SQL_ERROR       = 4
} SQLStmtType;

typedef struct {
    char name[SQL_MAX_NAME];
    SQLDataType type;
    int  length;
} ColumnDef;

typedef struct {
    char table[SQL_MAX_NAME];
    char name[SQL_MAX_NAME];
} ColumnRef;

typedef enum {
    SQL_CMP_EQ = 0, SQL_CMP_NE = 1,
    SQL_CMP_LT = 2, SQL_CMP_LE = 3,
    SQL_CMP_GT = 4, SQL_CMP_GE = 5,
    SQL_CMP_NONE = 6
} SQLCmpOp;

typedef struct {
    ColumnRef  col;
    SQLCmpOp   op;
    int        int_val;
    char       str_val[SQL_MAX_NAME];
} WhereClause;

typedef struct {
    SQLStmtType  type;
    char         table[SQL_MAX_NAME];

    int          num_columns;
    char         columns[SQL_MAX_COLUMNS][SQL_MAX_NAME];

    WhereClause  where_clause;
    int          has_where;

    int          has_order_by;
    char         order_by[SQL_MAX_NAME];

    int          num_col_defs;
    ColumnDef    col_defs[SQL_MAX_COLUMNS];

    int          num_values;
    char         values[SQL_MAX_COLUMNS][SQL_MAX_NAME];
} SQLStmt;

int  sql_parse(const char *sql, SQLStmt *stmt);
int  sql_parse_select(const char *sql, SQLStmt *stmt);
int  sql_parse_create(const char *sql, SQLStmt *stmt);
int  sql_parse_insert(const char *sql, SQLStmt *stmt);
int  sql_parse_drop(const char *sql, SQLStmt *stmt);
void sql_print_stmt(const SQLStmt *stmt);

#endif
