#ifndef QUERY_EXEC_H
#define QUERY_EXEC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define QE_MAX_TUPLES       4096
#define QE_MAX_COLUMNS      16
#define QE_MAX_NAME_LEN     32

/*
 * L7: Query Execution Engine — 火山模型 (Volcano Model)
 * 对应 CMU 15-445 Lecture 11-13: Query Execution
 *
 * 所有 Relational Operator 都实现 iterator 接口:
 *   - open(): 初始化算子
 *   - next(): 返回下一个 tuple (无更多返回 NULL)
 *   - close(): 清理资源
 *
 * 算子组合形成查询计划树 (Query Plan Tree):
 *   Project → Filter → SeqScan
 */

/* ---- Tuple (行) / Schema (表结构) ---- */

typedef struct {
    char name[QE_MAX_NAME_LEN];
    enum { QE_TYPE_INT32 = 0, QE_TYPE_FLOAT = 1, QE_TYPE_STRING = 2 } type;
    int32_t offset;
    int32_t length;
} ColumnDef;

typedef struct {
    ColumnDef columns[QE_MAX_COLUMNS];
    int32_t   num_columns;
    int32_t   tuple_size;
} TableSchema;

typedef struct {
    uint8_t *data;
    int32_t  size;
} QETuple;

/* ---- Operator Interface (Volcano Iterator Model) ---- */
typedef enum {
    QE_OP_SEQSCAN  = 0,
    QE_OP_FILTER   = 1,
    QE_OP_PROJECT  = 2,
    QE_OP_NLJOIN   = 3,
    QE_OP_SORT     = 4,
    QE_OP_LIMIT    = 5,
    QE_OP_AGGREGATE = 6
} QEOperatorType;

typedef struct QEOperator QEOperator;

struct QEOperator {
    QEOperatorType type;
    void (*open)(QEOperator *op);
    QETuple *(*next)(QEOperator *op);
    void (*close)(QEOperator *op);
    void  *state;
    QEOperator *child;
    QEOperator *child2;
};

/* ---- Operator Constructors ---- */
QEOperator *qe_seqscan_create(const uint8_t *tuples, int32_t num_tuples,
                               int32_t tuple_size, const TableSchema *schema);
QEOperator *qe_filter_create(QEOperator *child, const TableSchema *schema,
                              bool (*predicate)(const QETuple *t, const TableSchema *s));
QEOperator *qe_project_create(QEOperator *child, const TableSchema *in_schema,
                               TableSchema *out_schema, int32_t *col_indices, int32_t num_cols);
QEOperator *qe_nljoin_create(QEOperator *left, QEOperator *right,
                              const TableSchema *left_schema, const TableSchema *right_schema,
                              bool (*join_pred)(const QETuple *l, const QETuple *r,
                                                const TableSchema *ls, const TableSchema *rs),
                              TableSchema *out_schema);
QEOperator *qe_limit_create(QEOperator *child, int32_t limit_count);
QEOperator *qe_sort_create(QEOperator *child, const TableSchema *schema,
                            int32_t sort_col, bool ascending);

/* ---- Execution Helpers ---- */
void        qe_exec_print(QEOperator *root, const TableSchema *schema, int32_t max_rows);
int32_t     qe_get_column_value_int32(const QETuple *t, const TableSchema *s, int32_t col_idx);
float       qe_get_column_value_float(const QETuple *t, const TableSchema *s, int32_t col_idx);
const char *qe_get_column_value_string(const QETuple *t, const TableSchema *s, int32_t col_idx);
void        qe_destroy_operator(QEOperator *op);

#endif
