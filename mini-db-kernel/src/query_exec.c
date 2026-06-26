#include "query_exec.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/*
 * L7: Query Execution Engine — 火山模型 (Volcano Iterator Model)
 *
 * 火山模型由 Goetz Graefe 在 1994 年论文
 * "Volcano - An Extensible and Parallel Query Evaluation System" 中提出。
 *
 * 核心接口 (每个 Operator 实现):
 *   - open():  初始化
 *   - next():  返回下一个 tuple，无更多返回 NULL
 *   - close(): 清理
 *
 * 通过组合不同 Operator 形成查询计划树:
 *   Project(cols) → Filter(predicate) → SeqScan(table)
 *
 * 课程映射: CMU 15-445 Lecture 11-13 (Query Execution)
 *           Berkeley CS 186 — Query Processing
 *           Stanford CS 245 — Query Execution & Optimization
 */

/* ---- Memory Management for Tuples ---- */
static QETuple *qe_tuple_alloc(int32_t size) {
    QETuple *t = (QETuple *)malloc(sizeof(QETuple));
    if (!t) return NULL;
    t->data = (uint8_t *)malloc((size_t)size);
    if (!t->data) { free(t); return NULL; }
    t->size = size;
    return t;
}

static void qe_tuple_free(QETuple *t) {
    if (t) { free(t->data); free(t); }
}

static QETuple *qe_tuple_copy(const QETuple *src) {
    if (!src) return NULL;
    QETuple *dst = qe_tuple_alloc(src->size);
    if (!dst) return NULL;
    memcpy(dst->data, src->data, (size_t)src->size);
    return dst;
}

/* ---- SeqScan Operator ----
 * L2: 全表扫描 (Sequential Scan)
 *
 * 最简单的关系算子: 逐行输出表中的所有 tuple。
 * 对应 PostgreSQL 的 SeqScan 节点 (ExecSeqScan)。
 * 
 * 在真实系统中，SeqScan 通常包含:
 *   - 预取 (prefetch) 优化
 *   - 同步扫描 (synchronized seqscan) 减少 I/O
 *   - 可见性检查 (MVCC visibility)
 */

typedef struct {
    const uint8_t *tuples;
    int32_t  num_tuples;
    int32_t  tuple_size;
    int32_t  current;
} QESeqScanState;

static void seqscan_open(QEOperator *op) {
    QESeqScanState *s = (QESeqScanState *)op->state;
    s->current = 0;
}

static QETuple *seqscan_next(QEOperator *op) {
    QESeqScanState *s = (QESeqScanState *)op->state;
    if (s->current >= s->num_tuples) return NULL;

    QETuple *t = qe_tuple_alloc(s->tuple_size);
    if (!t) return NULL;
    memcpy(t->data, s->tuples + (size_t)s->current * (size_t)s->tuple_size, 
           (size_t)s->tuple_size);
    s->current++;
    return t;
}

static void seqscan_close(QEOperator *op) {
    /* No cleanup needed beyond state free */
    (void)op;
}

QEOperator *qe_seqscan_create(const uint8_t *tuples, int32_t num_tuples,
                               int32_t tuple_size, const TableSchema *schema) {
    (void)schema;
    QEOperator *op = (QEOperator *)calloc(1, sizeof(QEOperator));
    if (!op) return NULL;
    QESeqScanState *s = (QESeqScanState *)calloc(1, sizeof(QESeqScanState));
    if (!s) { free(op); return NULL; }

    s->tuples = tuples;
    s->num_tuples = num_tuples;
    s->tuple_size = tuple_size;
    s->current = 0;

    op->type = QE_OP_SEQSCAN;
    op->state = s;
    op->open = seqscan_open;
    op->next = seqscan_next;
    op->close = seqscan_close;
    op->child = NULL;
    return op;
}

/* ---- Filter Operator (Selection) ----
 * L2: 选择操作 (σ predicate)
 *
 * Filter 算子对其子算子的每个 tuple 应用谓词函数。
 * 只有通过谓词的 tuple 才被输出。
 *
 * 对应 SQL: SELECT * FROM table WHERE condition
 *           σ_{predicate}(R)
 *
 * 对应 PostgreSQL 的 ExecFilter 节点。
 * 在真实系统中:
 *   - 谓词下推 (pushdown) 到存储层
 *   - JIT 编译谓词 (PostgreSQL 的 LLVM JIT)
 *   - 向量化执行 (SIMD filter)
 */

typedef struct {
    bool (*predicate)(const QETuple *t, const TableSchema *s);
    const TableSchema *schema;
} QEFilterState;

static void filter_open(QEOperator *op) {
    if (op->child && op->child->open) op->child->open(op->child);
}

static QETuple *filter_next(QEOperator *op) {
    QEFilterState *fs = (QEFilterState *)op->state;
    while (1) {
        QETuple *t = op->child->next(op->child);
        if (!t) return NULL;
        if (fs->predicate(t, fs->schema)) return t;
        qe_tuple_free(t);
    }
}

static void filter_close(QEOperator *op) {
    if (op->child && op->child->close) op->child->close(op->child);
}

QEOperator *qe_filter_create(QEOperator *child, const TableSchema *schema,
                              bool (*predicate)(const QETuple *t, const TableSchema *s)) {
    QEOperator *op = (QEOperator *)calloc(1, sizeof(QEOperator));
    if (!op) return NULL;
    QEFilterState *s = (QEFilterState *)calloc(1, sizeof(QEFilterState));
    if (!s) { free(op); return NULL; }

    s->predicate = predicate;
    s->schema = schema;

    op->type = QE_OP_FILTER;
    op->state = s;
    op->open = filter_open;
    op->next = filter_next;
    op->close = filter_close;
    op->child = child;
    return op;
}

/* ---- Project Operator (Projection) ----
 * L2: 投影操作 (π columns)
 *
 * Project 算子从输入 tuple 中提取指定列，生成新的 tuple schema。
 *
 * 对应 SQL: SELECT a, b FROM table
 *           π_{a,b}(R)
 *
 * 本实现的 Project 仅支持列投影 (不包含表达式求值)。
 */

typedef struct {
    int32_t *col_indices;
    int32_t  num_cols;
    const TableSchema *in_schema;
    TableSchema *out_schema;
} QEProjectState;

static void project_open(QEOperator *op) {
    if (op->child && op->child->open) op->child->open(op->child);
}

static QETuple *project_next(QEOperator *op) {
    QEProjectState *ps = (QEProjectState *)op->state;
    QETuple *input = op->child->next(op->child);
    if (!input) return NULL;

    QETuple *output = qe_tuple_alloc(ps->out_schema->tuple_size);
    if (!output) { qe_tuple_free(input); return NULL; }
    memset(output->data, 0, (size_t)ps->out_schema->tuple_size);

    for (int32_t c = 0; c < ps->num_cols; c++) {
        int32_t src_col = ps->col_indices[c];
        if (src_col < 0 || src_col >= ps->in_schema->num_columns) continue;
        const ColumnDef *src = &ps->in_schema->columns[src_col];
        const ColumnDef *dst = &ps->out_schema->columns[c];
        memcpy(output->data + dst->offset, input->data + src->offset, 
               src->length < dst->length ? (size_t)src->length : (size_t)dst->length);
    }

    qe_tuple_free(input);
    return output;
}

static void project_close(QEOperator *op) {
    if (op->child && op->child->close) op->child->close(op->child);
}

QEOperator *qe_project_create(QEOperator *child, const TableSchema *in_schema,
                               TableSchema *out_schema, int32_t *col_indices, int32_t num_cols) {
    QEOperator *op = (QEOperator *)calloc(1, sizeof(QEOperator));
    if (!op) return NULL;
    QEProjectState *s = (QEProjectState *)calloc(1, sizeof(QEProjectState));
    if (!s) { free(op); return NULL; }

    s->col_indices = (int32_t *)malloc(sizeof(int32_t) * (size_t)num_cols);
    if (!s->col_indices) { free(s); free(op); return NULL; }
    memcpy(s->col_indices, col_indices, sizeof(int32_t) * (size_t)num_cols);
    s->num_cols = num_cols;
    s->in_schema = in_schema;
    s->out_schema = out_schema;

    op->type = QE_OP_PROJECT;
    op->state = s;
    op->open = project_open;
    op->next = project_next;
    op->close = project_close;
    op->child = child;
    return op;
}

/* ---- Nested Loop Join Operator ----
 * L5: 嵌套循环连接 (Nested Loop Join)
 *
 * 最简单的 Join 算法:
 *   for each tuple r in left:
 *     for each tuple s in right:
 *       if join_pred(r, s):
 *         output concat(r, s)
 *
 * 复杂度: O(|R| * |S|)  — 对两个输入集合的笛卡尔积
 *
 * 缺点: 大表连接极慢
 * 优点: 对任何 Join 条件都有效 (不限于等值连接)
 *
 * 本实现需要缓存右子算子的所有输出 (因为 Volcano 的 next() 是消耗性的)。
 *
 * 工程扩展:
 *   - Block NLJ: 每次读取一个 block 的左表，减少右表扫描
 *   - Index NLJ: 如果右表有索引，用索引查找替代扫描
 */

typedef struct {
    const TableSchema *left_schema;
    const TableSchema *right_schema;
    TableSchema *out_schema;
    bool (*join_pred)(const QETuple *l, const QETuple *r,
                       const TableSchema *ls, const TableSchema *rs);
    QETuple **right_tuples;
    int32_t  right_count;
    QETuple *current_left;
    int32_t  right_pos;
} QENLJoinState;

static void nljoin_open(QEOperator *op) {
    QENLJoinState *js = (QENLJoinState *)op->state;
    if (op->child && op->child->open) op->child->open(op->child);
    if (op->child2 && op->child2->open) op->child2->open(op->child2);

    /* 缓存右子算子的所有输出 */
    js->right_tuples = (QETuple **)malloc(sizeof(QETuple *) * (size_t)QE_MAX_TUPLES);
    js->right_count = 0;
    js->right_pos = 0;
    js->current_left = NULL;

    if (js->right_tuples && op->child2) {
        QETuple *t;
        while ((t = op->child2->next(op->child2)) && js->right_count < QE_MAX_TUPLES) {
            js->right_tuples[js->right_count++] = t;
        }
    }
}

static QETuple *nljoin_next(QEOperator *op) {
    QENLJoinState *js = (QENLJoinState *)op->state;
    if (!js->right_tuples || js->right_count == 0) return NULL;

    while (1) {
        /* 获取下一个左 tuple (如果需要) */
        if (!js->current_left) {
            js->current_left = op->child->next(op->child);
            if (!js->current_left) return NULL;
        }

        /* 扫描右表 */
        while (js->right_pos < js->right_count) {
            QETuple *r = js->right_tuples[js->right_pos];
            js->right_pos++;

            if (js->join_pred(js->current_left, r, js->left_schema, js->right_schema)) {
                /* 构造输出 tuple: left || right */
                QETuple *out = qe_tuple_alloc(js->out_schema->tuple_size);
                if (!out) return NULL;
                memset(out->data, 0, (size_t)js->out_schema->tuple_size);

                /* 复制左表列 */
                for (int32_t c = 0; c < js->left_schema->num_columns; c++) {
                    const ColumnDef *col = &js->left_schema->columns[c];
                    memcpy(out->data + col->offset, 
                           js->current_left->data + col->offset, (size_t)col->length);
                }
                /* 复制右表列 (offset 已经在 out_schema 中正确设置) */
                for (int32_t c = 0; c < js->right_schema->num_columns; c++) {
                    const ColumnDef *col = &js->right_schema->columns[c];
                    const ColumnDef *out_col = &js->out_schema->columns[
                        js->left_schema->num_columns + c];
                    memcpy(out->data + out_col->offset,
                           r->data + col->offset, (size_t)col->length);
                }
                return out;
            }
        }

        /* 右表扫描完毕，切换到下一个左 tuple */
        qe_tuple_free(js->current_left);
        js->current_left = NULL;
        js->right_pos = 0;
    }
}

static void nljoin_close(QEOperator *op) {
    QENLJoinState *js = (QENLJoinState *)op->state;
    if (js->right_tuples) {
        for (int32_t i = 0; i < js->right_count; i++) {
            qe_tuple_free(js->right_tuples[i]);
        }
        free(js->right_tuples);
        js->right_tuples = NULL;
    }
    if (js->current_left) qe_tuple_free(js->current_left);
    if (op->child && op->child->close) op->child->close(op->child);
    if (op->child2 && op->child2->close) op->child2->close(op->child2);
}

QEOperator *qe_nljoin_create(QEOperator *left, QEOperator *right,
                              const TableSchema *left_schema, const TableSchema *right_schema,
                              bool (*join_pred)(const QETuple *l, const QETuple *r,
                                                const TableSchema *ls, const TableSchema *rs),
                              TableSchema *out_schema) {
    QEOperator *op = (QEOperator *)calloc(1, sizeof(QEOperator));
    if (!op) return NULL;
    QENLJoinState *s = (QENLJoinState *)calloc(1, sizeof(QENLJoinState));
    if (!s) { free(op); return NULL; }

    s->left_schema = left_schema;
    s->right_schema = right_schema;
    s->out_schema = out_schema;
    s->join_pred = join_pred;

    op->type = QE_OP_NLJOIN;
    op->state = s;
    op->open = nljoin_open;
    op->next = nljoin_next;
    op->close = nljoin_close;
    op->child = left;
    op->child2 = right;
    return op;
}

/* ---- Limit Operator ----
 * L7: 限制输出行数
 * 对应 SQL: SELECT ... LIMIT n
 */

typedef struct {
    int32_t limit_count;
    int32_t emitted;
} QELimitState;

static void limit_open(QEOperator *op) {
    QELimitState *s = (QELimitState *)op->state;
    s->emitted = 0;
    if (op->child && op->child->open) op->child->open(op->child);
}

static QETuple *limit_next(QEOperator *op) {
    QELimitState *s = (QELimitState *)op->state;
    if (s->emitted >= s->limit_count) return NULL;
    QETuple *t = op->child->next(op->child);
    if (t) s->emitted++;
    return t;
}

static void limit_close(QEOperator *op) {
    if (op->child && op->child->close) op->child->close(op->child);
}

QEOperator *qe_limit_create(QEOperator *child, int32_t limit_count) {
    QEOperator *op = (QEOperator *)calloc(1, sizeof(QEOperator));
    if (!op) return NULL;
    QELimitState *s = (QELimitState *)calloc(1, sizeof(QELimitState));
    if (!s) { free(op); return NULL; }

    s->limit_count = limit_count;
    s->emitted = 0;

    op->type = QE_OP_LIMIT;
    op->state = s;
    op->open = limit_open;
    op->next = limit_next;
    op->close = limit_close;
    op->child = child;
    return op;
}

/* ---- Sort Operator ----
 * L5: 排序算子 (基于插入排序，适合少量数据)
 *
 * 对应 SQL: SELECT ... ORDER BY col
 * 内嵌排序将所有数据收集到内存后排序
 *
 * 真实系统使用:
 *   - 外部排序 (External Sort): 数据大于内存时使用
 *   - 快速排序 (QuickSort): MySQL/PostgreSQL 使用
 *   - 基数排序 (Radix Sort): 某些列存系统
 */

typedef struct {
    int32_t  sort_col;
    bool     ascending;
    QETuple **buffer;
    int32_t  buf_count;
    int32_t  buf_capacity;
    int32_t  pos;
    const TableSchema *schema;
} QESortState;

static void sort_open(QEOperator *op) {
    QESortState *ss = (QESortState *)op->state;
    ss->buffer = (QETuple **)malloc(sizeof(QETuple *) * (size_t)QE_MAX_TUPLES);
    ss->buf_count = 0;
    ss->buf_capacity = QE_MAX_TUPLES;
    ss->pos = 0;

    if (op->child && op->child->open) op->child->open(op->child);

    /* 收集所有 tuple */
    if (ss->buffer) {
        QETuple *t;
        while ((t = op->child->next(op->child)) && ss->buf_count < ss->buf_capacity) {
            ss->buffer[ss->buf_count++] = t;
        }
    }

    /* 简单排序 (基于 sort_col 在 offset 0 处的值) */
    for (int32_t i = 0; i < ss->buf_count - 1; i++) {
        for (int32_t j = i + 1; j < ss->buf_count; j++) {
            int32_t vi = *(int32_t *)(ss->buffer[i]->data + 
                (size_t)ss->schema->columns[ss->sort_col].offset);
            int32_t vj = *(int32_t *)(ss->buffer[j]->data + 
                (size_t)ss->schema->columns[ss->sort_col].offset);
            bool should_swap = ss->ascending ? (vi > vj) : (vi < vj);
            if (should_swap) {
                QETuple *tmp = ss->buffer[i];
                ss->buffer[i] = ss->buffer[j];
                ss->buffer[j] = tmp;
            }
        }
    }
}

static QETuple *sort_next(QEOperator *op) {
    QESortState *ss = (QESortState *)op->state;
    if (ss->pos >= ss->buf_count) return NULL;
    return qe_tuple_copy(ss->buffer[ss->pos++]);
}

static void sort_close(QEOperator *op) {
    QESortState *ss = (QESortState *)op->state;
    if (ss->buffer) {
        for (int32_t i = 0; i < ss->buf_count; i++) {
            qe_tuple_free(ss->buffer[i]);
        }
        free(ss->buffer);
        ss->buffer = NULL;
    }
    if (op->child && op->child->close) op->child->close(op->child);
}

QEOperator *qe_sort_create(QEOperator *child, const TableSchema *schema,
                            int32_t sort_col, bool ascending) {
    QEOperator *op = (QEOperator *)calloc(1, sizeof(QEOperator));
    if (!op) return NULL;
    QESortState *s = (QESortState *)calloc(1, sizeof(QESortState));
    if (!s) { free(op); return NULL; }

    s->sort_col = sort_col;
    s->ascending = ascending;
    s->schema = schema;

    op->type = QE_OP_SORT;
    op->state = s;
    op->open = sort_open;
    op->next = sort_next;
    op->close = sort_close;
    op->child = child;
    return op;
}

/* ---- Execution Helpers ---- */

void qe_exec_print(QEOperator *root, const TableSchema *schema, int32_t max_rows) {
    if (!root || !schema) return;

    /* Print header */
    for (int32_t c = 0; c < schema->num_columns; c++) {
        printf("%-12s ", schema->columns[c].name);
    }
    printf("\n");
    for (int32_t c = 0; c < schema->num_columns; c++) {
        printf("------------ ");
    }
    printf("\n");

    /* Print rows */
    root->open(root);
    int32_t count = 0;
    QETuple *t;
    while ((t = root->next(root)) && count < max_rows) {
        for (int32_t c = 0; c < schema->num_columns; c++) {
            switch (schema->columns[c].type) {
                case QE_TYPE_INT32:
                    printf("%-12d ", *(int32_t *)(t->data + schema->columns[c].offset));
                    break;
                case QE_TYPE_FLOAT:
                    printf("%-12.2f ", (double)*(float *)(t->data + schema->columns[c].offset));
                    break;
                case QE_TYPE_STRING:
                    printf("%-12s ", (char *)(t->data + schema->columns[c].offset));
                    break;
                default:
                    printf("%-12s ", "?");
                    break;
            }
        }
        printf("\n");
        qe_tuple_free(t);
        count++;
    }
    printf("(%d rows)\n", count);
    root->close(root);
}

int32_t qe_get_column_value_int32(const QETuple *t, const TableSchema *s, int32_t col_idx) {
    if (!t || !s || col_idx < 0 || col_idx >= s->num_columns) return 0;
    return *(int32_t *)(t->data + s->columns[col_idx].offset);
}

float qe_get_column_value_float(const QETuple *t, const TableSchema *s, int32_t col_idx) {
    if (!t || !s || col_idx < 0 || col_idx >= s->num_columns) return 0.0f;
    return *(float *)(t->data + s->columns[col_idx].offset);
}

const char *qe_get_column_value_string(const QETuple *t, const TableSchema *s, int32_t col_idx) {
    if (!t || !s || col_idx < 0 || col_idx >= s->num_columns) return "(null)";
    return (const char *)(t->data + s->columns[col_idx].offset);
}

void qe_destroy_operator(QEOperator *op) {
    if (!op) return;
    /* 递归销毁子算子 */
    if (op->child) qe_destroy_operator(op->child);
    if (op->child2) qe_destroy_operator(op->child2);

    /* 释放状态 */
    if (op->state) {
        if (op->type == QE_OP_PROJECT) {
            QEProjectState *s = (QEProjectState *)op->state;
            free(s->col_indices);
        }
        free(op->state);
    }
    free(op);
}