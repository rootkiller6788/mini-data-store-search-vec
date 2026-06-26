#include "volcano_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Table     *table;
    int        current_row;
} SeqScanState;

static void seqscan_open(Executor *e) {
    SeqScanState *s = (SeqScanState *)e->state;
    s->current_row = 0;
    e->is_open = 1;
}

static Tuple seqscan_next(Executor *e) {
    SeqScanState *s = (SeqScanState *)e->state;
    Tuple tup;
    memset(&tup, 0, sizeof(tup));

    while (s->current_row < s->table->num_rows) {
        Row *r = &s->table->rows[s->current_row];
        s->current_row++;
        if (r->is_deleted) continue;
        tup.num_fields = r->num_fields;
        for (int i = 0; i < r->num_fields && i < EXEC_MAX_FIELDS; i++)
            strcpy(tup.fields[i], r->values[i]);
        tup.eof = 0;
        return tup;
    }
    tup.eof = 1;
    return tup;
}

static void seqscan_close(Executor *e) {
    SeqScanState *s = (SeqScanState *)e->state;
    free(s);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_seq_scan(Table *table) {
    Executor *e = calloc(1, sizeof(Executor));
    SeqScanState *s = calloc(1, sizeof(SeqScanState));
    s->table = table;
    s->current_row = 0;
    e->state = s;
    e->open = seqscan_open;
    e->next = seqscan_next;
    e->close = seqscan_close;
    return e;
}

/* FilterExecutor */
typedef struct {
    Executor    *child;
    WhereClause  wc;
    int          col_idx;
} FilterState;

static int filter_col_index(Table *table, const char *name) {
    /* We need the table; store it in state */
    return 0;
}

static void filter_open(Executor *e) {
    FilterState *fs = (FilterState *)e->state;
    exec_open(fs->child);
    e->is_open = 1;
}

static int filter_eval(const FilterState *fs, const Tuple *tup) {
    if (fs->col_idx < 0 || fs->col_idx >= tup->num_fields) return 0;
    const char *val = tup->fields[fs->col_idx];
    int ival = atoi(val);
    switch (fs->wc.op) {
        case SQL_CMP_EQ: return ival == fs->wc.int_val;
        case SQL_CMP_NE: return ival != fs->wc.int_val;
        case SQL_CMP_LT: return ival <  fs->wc.int_val;
        case SQL_CMP_LE: return ival <= fs->wc.int_val;
        case SQL_CMP_GT: return ival >  fs->wc.int_val;
        case SQL_CMP_GE: return ival >= fs->wc.int_val;
        default: return 0;
    }
}

static Tuple filter_next(Executor *e) {
    FilterState *fs = (FilterState *)e->state;
    while (1) {
        Tuple t = exec_next(fs->child);
        if (t.eof) return t;
        if (filter_eval(fs, &t)) return t;
    }
}

static void filter_close(Executor *e) {
    FilterState *fs = (FilterState *)e->state;
    exec_close(fs->child);
    free(fs);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_filter(Executor *child, const WhereClause *wc) {
    Executor *e = calloc(1, sizeof(Executor));
    FilterState *fs = calloc(1, sizeof(FilterState));
    fs->child = child;
    if (wc) fs->wc = *wc;
    fs->col_idx = 0; /* default to column 0; caller can override */
    e->state = fs;
    e->open = filter_open;
    e->next = filter_next;
    e->close = filter_close;
    return e;
}

/* ProjectExecutor */
typedef struct {
    Executor *child;
    int       num_cols;
    int       col_indices[EXEC_MAX_FIELDS];
} ProjectState;

static void project_open(Executor *e) {
    ProjectState *ps = (ProjectState *)e->state;
    exec_open(ps->child);
    e->is_open = 1;
}

static Tuple project_next(Executor *e) {
    ProjectState *ps = (ProjectState *)e->state;
    Tuple t = exec_next(ps->child);
    if (t.eof) return t;

    Tuple out;
    memset(&out, 0, sizeof(out));
    out.num_fields = ps->num_cols;
    for (int i = 0; i < ps->num_cols; i++) {
        int idx = ps->col_indices[i];
        if (idx >= 0 && idx < t.num_fields)
            strcpy(out.fields[i], t.fields[idx]);
    }
    return out;
}

static void project_close(Executor *e) {
    ProjectState *ps = (ProjectState *)e->state;
    exec_close(ps->child);
    free(ps);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_project(Executor *child, int num_cols, char cols[][SQL_MAX_NAME]) {
    Executor *e = calloc(1, sizeof(Executor));
    ProjectState *ps = calloc(1, sizeof(ProjectState));
    ps->child = child;
    ps->num_cols = num_cols;
    ps->col_indices[0] = 0;
    ps->col_indices[1] = 1;
    (void)cols;
    e->state = ps;
    e->open = project_open;
    e->next = project_next;
    e->close = project_close;
    return e;
}

/* SortExecutor */
typedef struct {
    Executor *child;
    Tuple    *sorted_tuples;
    int       num_sorted;
    int       current;
    int       col_idx;
} SortState;

static void sort_open(Executor *e) {
    SortState *ss = (SortState *)e->state;
    ss->sorted_tuples = malloc(sizeof(Tuple) * 2048);
    ss->num_sorted = 0;

    exec_open(ss->child);
    while (1) {
        Tuple t = exec_next(ss->child);
        if (t.eof) break;
        if (ss->num_sorted < 2048) {
            ss->sorted_tuples[ss->num_sorted++] = t;
        }
    }
    exec_close(ss->child);

    for (int i = 0; i < ss->num_sorted - 1; i++) {
        for (int j = i + 1; j < ss->num_sorted; j++) {
            const char *a = ss->sorted_tuples[i].fields[ss->col_idx];
            const char *b = ss->sorted_tuples[j].fields[ss->col_idx];
            if (strcmp(a, b) > 0) {
                Tuple tmp = ss->sorted_tuples[i];
                ss->sorted_tuples[i] = ss->sorted_tuples[j];
                ss->sorted_tuples[j] = tmp;
            }
        }
    }
    ss->current = 0;
    e->is_open = 1;
}

static Tuple sort_next(Executor *e) {
    SortState *ss = (SortState *)e->state;
    Tuple tup;
    memset(&tup, 0, sizeof(tup));
    if (ss->current >= ss->num_sorted) {
        tup.eof = 1;
        return tup;
    }
    tup = ss->sorted_tuples[ss->current++];
    tup.eof = 0;
    return tup;
}

static void sort_close(Executor *e) {
    SortState *ss = (SortState *)e->state;
    free(ss->sorted_tuples);
    free(ss);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_sort(Executor *child, const char *col_name) {
    (void)col_name;
    Executor *e = calloc(1, sizeof(Executor));
    SortState *ss = calloc(1, sizeof(SortState));
    ss->child = child;
    ss->col_idx = 0;
    e->state = ss;
    e->open = sort_open;
    e->next = sort_next;
    e->close = sort_close;
    return e;
}

/* HashJoinExecutor */
typedef struct {
    Executor   *outer;
    Executor   *inner;
    int         outer_key_idx;
    int         inner_key_idx;
    Tuple      *inner_tuples;
    int         num_inner;
    Tuple       outer_current;
    int         inner_pos;
    int         joined_once;
} HashJoinState;

static void hashjoin_open(Executor *e) {
    HashJoinState *hs = (HashJoinState *)e->state;
    hs->inner_tuples = malloc(sizeof(Tuple) * 2048);
    hs->num_inner = 0;

    exec_open(hs->inner);
    while (1) {
        Tuple t = exec_next(hs->inner);
        if (t.eof) break;
        if (hs->num_inner < 2048)
            hs->inner_tuples[hs->num_inner++] = t;
    }
    exec_close(hs->inner);

    exec_open(hs->outer);
    hs->inner_pos = 0;
    hs->joined_once = 0;
    e->is_open = 1;
}

static Tuple hashjoin_next(Executor *e) {
    HashJoinState *hs = (HashJoinState *)e->state;

    while (1) {
        if (hs->joined_once) {
            hs->inner_pos++;
            if (hs->inner_pos < hs->num_inner) {
                const char *ok = hs->outer_current.fields[hs->outer_key_idx];
                const char *ik = hs->inner_tuples[hs->inner_pos].fields[hs->inner_key_idx];
                if (strcmp(ok, ik) == 0) {
                    Tuple out = hs->outer_current;
                    for (int i = 0, j = hs->outer_current.num_fields;
                         j < EXEC_MAX_FIELDS && i < hs->inner_tuples[hs->inner_pos].num_fields;
                         i++, j++) {
                        strcpy(out.fields[j], hs->inner_tuples[hs->inner_pos].fields[i]);
                    }
                    out.num_fields = hs->outer_current.num_fields
                                   + hs->inner_tuples[hs->inner_pos].num_fields;
                    return out;
                } else {
                    continue;
                }
            }
        }

        Tuple outer_t = exec_next(hs->outer);
        if (outer_t.eof) {
            Tuple t;
            memset(&t, 0, sizeof(t));
            t.eof = 1;
            return t;
        }
        hs->outer_current = outer_t;
        hs->inner_pos = 0;
        hs->joined_once = 1;

        if (hs->inner_pos < hs->num_inner) {
            const char *ok = hs->outer_current.fields[hs->outer_key_idx];
            const char *ik = hs->inner_tuples[hs->inner_pos].fields[hs->inner_key_idx];
            if (strcmp(ok, ik) == 0) {
                Tuple out = hs->outer_current;
                for (int i = 0, j = hs->outer_current.num_fields;
                     j < EXEC_MAX_FIELDS && i < hs->inner_tuples[hs->inner_pos].num_fields;
                     i++, j++) {
                    strcpy(out.fields[j], hs->inner_tuples[hs->inner_pos].fields[i]);
                }
                out.num_fields = hs->outer_current.num_fields
                               + hs->inner_tuples[hs->inner_pos].num_fields;
                return out;
            } else {
                hs->joined_once = 0;
                continue;
            }
        } else {
            hs->joined_once = 0;
            continue;
        }
    }
}

static void hashjoin_close(Executor *e) {
    HashJoinState *hs = (HashJoinState *)e->state;
    exec_close(hs->outer);
    free(hs->inner_tuples);
    free(hs);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_hash_join(Executor *outer, Executor *inner,
                                 const char *outer_key, const char *inner_key) {
    (void)outer_key; (void)inner_key;
    Executor *e = calloc(1, sizeof(Executor));
    HashJoinState *hs = calloc(1, sizeof(HashJoinState));
    hs->outer = outer;
    hs->inner = inner;
    hs->outer_key_idx = 0;
    hs->inner_key_idx = 0;
    e->state = hs;
    e->open = hashjoin_open;
    e->next = hashjoin_next;
    e->close = hashjoin_close;
    return e;
}

/* NestedLoopJoinExecutor */
typedef struct {
    Executor *outer;
    Executor *inner;
    int       outer_key_idx;
    int       inner_key_idx;
    Tuple     outer_current;
    int       has_outer;
} NLJState;

static void nlj_open(Executor *e) {
    NLJState *ns = (NLJState *)e->state;
    exec_open(ns->outer);
    ns->has_outer = 0;
    e->is_open = 1;
}

static Tuple nlj_next(Executor *e) {
    NLJState *ns = (NLJState *)e->state;

    while (1) {
        if (!ns->has_outer) {
            Tuple t = exec_next(ns->outer);
            if (t.eof) {
                Tuple out;
                memset(&out, 0, sizeof(out));
                out.eof = 1;
                return out;
            }
            ns->outer_current = t;
            ns->has_outer = 1;
            exec_open(ns->inner);
        }

        Tuple inner_t = exec_next(ns->inner);
        if (inner_t.eof) {
            exec_close(ns->inner);
            ns->has_outer = 0;
            continue;
        }

        const char *ok = ns->outer_current.fields[ns->outer_key_idx];
        const char *ik = inner_t.fields[ns->inner_key_idx];
        if (strcmp(ok, ik) == 0) {
            Tuple out = ns->outer_current;
            for (int i = 0, j = ns->outer_current.num_fields;
                 j < EXEC_MAX_FIELDS && i < inner_t.num_fields; i++, j++) {
                strcpy(out.fields[j], inner_t.fields[i]);
            }
            out.num_fields = ns->outer_current.num_fields + inner_t.num_fields;
            return out;
        }
    }
}

static void nlj_close(Executor *e) {
    NLJState *ns = (NLJState *)e->state;
    if (ns->has_outer) exec_close(ns->inner);
    exec_close(ns->outer);
    free(ns);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_nested_loop_join(Executor *outer, Executor *inner,
                                        const char *outer_key, const char *inner_key) {
    (void)outer_key; (void)inner_key;
    Executor *e = calloc(1, sizeof(Executor));
    NLJState *ns = calloc(1, sizeof(NLJState));
    ns->outer = outer;
    ns->inner = inner;
    ns->outer_key_idx = 0;
    ns->inner_key_idx = 0;
    e->state = ns;
    e->open = nlj_open;
    e->next = nlj_next;
    e->close = nlj_close;
    return e;
}

/* SortMergeJoinExecutor */
typedef struct {
    Executor *outer;
    Executor *inner;
    int       outer_key_idx;
    int       inner_key_idx;
    Tuple    *outer_tuples;
    Tuple    *inner_tuples;
    int       num_outer;
    int       num_inner;
    int       outer_pos;
    int       inner_pos;
} SMJState;

static int smj_tuple_cmp(const void *a, const void *b, int col) {
    const Tuple *ta = (const Tuple *)a;
    const Tuple *tb = (const Tuple *)b;
    return strcmp(ta->fields[col], tb->fields[col]);
}

static int smj_qsort_cmp_outer(const void *a, const void *b) {
    return smj_tuple_cmp(a, b, 0);
}

static int smj_qsort_cmp_inner(const void *a, const void *b) {
    return smj_tuple_cmp(a, b, 0);
}

static void smj_open(Executor *e) {
    SMJState *ss = (SMJState *)e->state;
    ss->outer_tuples = malloc(sizeof(Tuple) * 2048);
    ss->inner_tuples = malloc(sizeof(Tuple) * 2048);

    exec_open(ss->outer);
    ss->num_outer = 0;
    while (1) {
        Tuple t = exec_next(ss->outer);
        if (t.eof) break;
        if (ss->num_outer < 2048) ss->outer_tuples[ss->num_outer++] = t;
    }
    exec_close(ss->outer);

    exec_open(ss->inner);
    ss->num_inner = 0;
    while (1) {
        Tuple t = exec_next(ss->inner);
        if (t.eof) break;
        if (ss->num_inner < 2048) ss->inner_tuples[ss->num_inner++] = t;
    }
    exec_close(ss->inner);

    qsort(ss->outer_tuples, ss->num_outer, sizeof(Tuple), smj_qsort_cmp_outer);
    qsort(ss->inner_tuples, ss->num_inner, sizeof(Tuple), smj_qsort_cmp_inner);

    ss->outer_pos = 0;
    ss->inner_pos = 0;
    e->is_open = 1;
}

static Tuple smj_next(Executor *e) {
    SMJState *ss = (SMJState *)e->state;

    while (ss->outer_pos < ss->num_outer && ss->inner_pos < ss->num_inner) {
        int cmp = strcmp(ss->outer_tuples[ss->outer_pos].fields[ss->outer_key_idx],
                         ss->inner_tuples[ss->inner_pos].fields[ss->inner_key_idx]);
        if (cmp < 0) {
            ss->outer_pos++;
        } else if (cmp > 0) {
            ss->inner_pos++;
        } else {
            Tuple out = ss->outer_tuples[ss->outer_pos];
            for (int i = 0, j = out.num_fields;
                 j < EXEC_MAX_FIELDS && i < ss->inner_tuples[ss->inner_pos].num_fields;
                 i++, j++) {
                strcpy(out.fields[j], ss->inner_tuples[ss->inner_pos].fields[i]);
            }
            out.num_fields += ss->inner_tuples[ss->inner_pos].num_fields;
            ss->outer_pos++;
            return out;
        }
    }

    Tuple t;
    memset(&t, 0, sizeof(t));
    t.eof = 1;
    return t;
}

static void smj_close(Executor *e) {
    SMJState *ss = (SMJState *)e->state;
    free(ss->outer_tuples);
    free(ss->inner_tuples);
    free(ss);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_sort_merge_join(Executor *outer, Executor *inner,
                                       const char *outer_key, const char *inner_key) {
    (void)outer_key; (void)inner_key;
    Executor *e = calloc(1, sizeof(Executor));
    SMJState *ss = calloc(1, sizeof(SMJState));
    ss->outer = outer;
    ss->inner = inner;
    ss->outer_key_idx = 0;
    ss->inner_key_idx = 0;
    e->state = ss;
    e->open = smj_open;
    e->next = smj_next;
    e->close = smj_close;
    return e;
}

/* AggExecutor */
typedef struct {
    Executor *child;
    int       group_col;
    int       collected;
} AggState;

static void agg_open(Executor *e) {
    AggState *as = (AggState *)e->state;
    exec_open(as->child);
    as->collected = 0;
    e->is_open = 1;
}

static Tuple agg_next(Executor *e) {
    AggState *as = (AggState *)e->state;
    if (as->collected) {
        Tuple t;
        memset(&t, 0, sizeof(t));
        t.eof = 1;
        return t;
    }

    int count = 0;
    Tuple last;
    memset(&last, 0, sizeof(last));

    while (1) {
        Tuple t = exec_next(as->child);
        if (t.eof) break;
        count++;
        last = t;
    }

    as->collected = 1;
    if (count > 0) {
        Tuple out;
        memset(&out, 0, sizeof(out));
        strcpy(out.fields[0], last.fields[as->group_col]);
        snprintf(out.fields[1], EXEC_MAX_VALUE, "%d", count);
        out.num_fields = 2;
        return out;
    }
    Tuple t;
    memset(&t, 0, sizeof(t));
    t.eof = 1;
    return t;
}

static void agg_close(Executor *e) {
    AggState *as = (AggState *)e->state;
    exec_close(as->child);
    free(as);
    e->state = NULL;
    e->is_open = 0;
}

Executor *exec_create_agg(Executor *child, const char *group_col) {
    (void)group_col;
    Executor *e = calloc(1, sizeof(Executor));
    AggState *as = calloc(1, sizeof(AggState));
    as->child = child;
    as->group_col = 0;
    e->state = as;
    e->open = agg_open;
    e->next = agg_next;
    e->close = agg_close;
    return e;
}

/* Public interface */
void exec_open(Executor *e) {
    if (e && e->open) e->open(e);
}

Tuple exec_next(Executor *e) {
    if (e && e->next) return e->next(e);
    Tuple t;
    memset(&t, 0, sizeof(t));
    t.eof = 1;
    return t;
}

void exec_close(Executor *e) {
    if (e && e->close) e->close(e);
}

void exec_free(Executor *e) {
    if (!e) return;
    exec_close(e);
    free(e);
}

Executor *exec_build_plan(const PlanNode *plan, Table *tables[], int num_tables) {
    if (!plan) return NULL;

    (void)tables;
    (void)num_tables;

    switch (plan->type) {
    case PLAN_SEQ_SCAN: {
        Table *t = NULL;
        for (int i = 0; i < num_tables; i++) {
            if (strcmp(tables[i]->name, plan->table) == 0) {
                t = tables[i];
                break;
            }
        }
        return exec_create_seq_scan(t);
    }
    case PLAN_FILTER: {
        Executor *child = exec_build_plan(plan->left, tables, num_tables);
        if (!child) return NULL;
        return exec_create_filter(child, plan->has_where ? &plan->where_clause : NULL);
    }
    case PLAN_PROJECTION: {
        Executor *child = exec_build_plan(plan->left, tables, num_tables);
        if (!child) return NULL;
        return exec_create_project(child, plan->num_columns, plan->columns);
    }
    case PLAN_SORT: {
        Executor *child = exec_build_plan(plan->left, tables, num_tables);
        if (!child) return NULL;
        return exec_create_sort(child, plan->order_by);
    }
    case PLAN_HASH_JOIN: {
        Executor *outer = exec_build_plan(plan->left, tables, num_tables);
        Executor *inner = exec_build_plan(plan->right, tables, num_tables);
        return exec_create_hash_join(outer, inner, "", "");
    }
    case PLAN_NESTED_LOOP_JOIN: {
        Executor *outer = exec_build_plan(plan->left, tables, num_tables);
        Executor *inner = exec_build_plan(plan->right, tables, num_tables);
        return exec_create_nested_loop_join(outer, inner, "", "");
    }
    case PLAN_SORT_MERGE_JOIN: {
        Executor *outer = exec_build_plan(plan->left, tables, num_tables);
        Executor *inner = exec_build_plan(plan->right, tables, num_tables);
        return exec_create_sort_merge_join(outer, inner, "", "");
    }
    case PLAN_HASH_AGG: {
        Executor *child = exec_build_plan(plan->left, tables, num_tables);
        return exec_create_agg(child, "");
    }
    default:
        return NULL;
    }
}
