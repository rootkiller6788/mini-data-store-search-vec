#ifndef VOLCANO_EXECUTOR_H
#define VOLCANO_EXECUTOR_H

#include "row_store.h"
#include "sql_parser.h"
#include "query_optimizer.h"
#include <stddef.h>

#define EXEC_MAX_FIELDS 16
#define EXEC_MAX_VALUE  64

typedef struct {
    char   fields[EXEC_MAX_FIELDS][EXEC_MAX_VALUE];
    int    num_fields;
    int    eof;
} Tuple;

typedef struct Executor Executor;

typedef void (*ExecOpenFn)(Executor *e);
typedef Tuple (*ExecNextFn)(Executor *e);
typedef void (*ExecCloseFn)(Executor *e);

struct Executor {
    ExecOpenFn   open;
    ExecNextFn   next;
    ExecCloseFn  close;

    void  *state;
    Executor *child;
    Executor *outer;
    Executor *inner;

    int    is_open;
    int    initialized;
};

Executor *exec_create_seq_scan(Table *table);
Executor *exec_create_filter(Executor *child, const WhereClause *wc);
Executor *exec_create_project(Executor *child, int num_cols, const char cols[][SQL_MAX_NAME]);
Executor *exec_create_sort(Executor *child, const char *col_name);
Executor *exec_create_hash_join(Executor *outer, Executor *inner,
                                 const char *outer_key, const char *inner_key);
Executor *exec_create_nested_loop_join(Executor *outer, Executor *inner,
                                        const char *outer_key, const char *inner_key);
Executor *exec_create_sort_merge_join(Executor *outer, Executor *inner,
                                       const char *outer_key, const char *inner_key);
Executor *exec_create_agg(Executor *child, const char *group_col);

void  exec_open(Executor *e);
Tuple exec_next(Executor *e);
void  exec_close(Executor *e);
void  exec_free(Executor *e);

Executor *exec_build_plan(const PlanNode *plan, Table *tables[], int num_tables);

#endif
