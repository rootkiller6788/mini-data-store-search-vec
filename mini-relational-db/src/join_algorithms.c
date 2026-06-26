#include "join_algorithms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int hash_string(const char *str) {
    unsigned int h = 5381;
    int c;
    while ((c = *str++))
        h = ((h << 5) + h) + (unsigned char)c;
    return h % JOIN_HT_SIZE;
}

void hash_table_init(HashTable *ht, int key_col) {
    memset(ht, 0, sizeof(HashTable));
    ht->key_col = key_col;
}

void hash_table_insert(HashTable *ht, const Row *row) {
    if (!row || ht->key_col < 0 || ht->key_col >= row->num_fields) return;
    const char *key = row->values[ht->key_col];
    unsigned int idx = hash_string(key);

    JoinTuple *jt = malloc(sizeof(JoinTuple));
    jt->row = *row;
    jt->next = NULL;

    JoinBucket *b = &ht->buckets[idx];
    if (!b->head) {
        b->head = jt;
        b->tail = jt;
    } else {
        b->tail->next = jt;
        b->tail = jt;
    }
    b->count++;
}

int hash_table_lookup(HashTable *ht, const char *key, JoinTuple **result) {
    if (!key) return 0;
    unsigned int idx = hash_string(key);
    JoinBucket *b = &ht->buckets[idx];
    JoinTuple *jt = b->head;
    int count = 0;
    while (jt) {
        if (strcmp(jt->row.values[ht->key_col], key) == 0) {
            if (result) {
                result[count] = jt;
            }
            count++;
        }
        jt = jt->next;
    }
    return count;
}

void hash_table_destroy(HashTable *ht) {
    for (int i = 0; i < JOIN_HT_SIZE; i++) {
        JoinTuple *jt = ht->buckets[i].head;
        while (jt) {
            JoinTuple *next = jt->next;
            free(jt);
            jt = next;
        }
        ht->buckets[i].head = NULL;
        ht->buckets[i].tail = NULL;
        ht->buckets[i].count = 0;
    }
}

int join_hash_build(HashTable *ht, Table *inner, const char *inner_key) {
    if (!ht || !inner || !inner_key) return -1;

    int col = table_find_col(inner, inner_key);
    if (col < 0) return -1;

    hash_table_init(ht, col);

    for (int i = 0; i < inner->num_rows; i++) {
        if (inner->rows[i].is_deleted) continue;
        hash_table_insert(ht, &inner->rows[i]);
    }
    return 0;
}

int join_hash_probe(HashTable *ht, Table *outer, const char *outer_key,
                     Table *result, JoinComparison *cmp) {
    if (!ht || !outer || !outer_key || !result) return -1;

    int col = table_find_col(outer, outer_key);
    if (col < 0) return -1;

    double compare_ops = 0;
    double io_pages = outer->num_rows * 0.01 + 1.0;
    int tuples_out = 0;

    for (int i = 0; i < outer->num_rows; i++) {
        if (outer->rows[i].is_deleted) continue;
        const char *key = outer->rows[i].values[col];
        JoinTuple *matches[JOIN_MAX_TUPLES];
        int n = hash_table_lookup(ht, key, matches);
        compare_ops += 1.0;

        for (int m = 0; m < n; m++) {
            Row combined;
            memset(&combined, 0, sizeof(Row));
            combined.num_fields = outer->rows[i].num_fields + matches[m]->row.num_fields;
            for (int f = 0; f < outer->rows[i].num_fields; f++)
                strcpy(combined.values[f], outer->rows[i].values[f]);
            for (int f = 0; f < matches[m]->row.num_fields; f++)
                strcpy(combined.values[f + outer->rows[i].num_fields],
                       matches[m]->row.values[f]);
            if (result->num_rows < TABLE_MAX_ROWS) {
                result->rows[result->num_rows++] = combined;
                tuples_out++;
            }
        }
    }

    if (cmp) {
        cmp->type = JOIN_HASH;
        cmp->rows_outer = outer->num_rows;
        cmp->rows_inner = inner_rows_from_ht(ht);
        cmp->compare_ops = compare_ops;
        cmp->hash_ops = (double)(outer->num_rows + inner_rows_from_ht(ht));
        cmp->io_pages = io_pages;
        snprintf(cmp->description, sizeof(cmp->description),
                 "Hash Join: outer=%.0f inner=%.0f compare=%.0f hash=%.0f result=%d",
                 cmp->rows_outer, cmp->rows_inner, cmp->compare_ops,
                 cmp->hash_ops, tuples_out);
    }
    return tuples_out;
}

static int inner_rows_from_ht(HashTable *ht) {
    int total = 0;
    for (int i = 0; i < JOIN_HT_SIZE; i++)
        total += ht->buckets[i].count;
    return total;
}

int join_nested_loop(Table *outer, Table *inner,
                      const char *outer_key, const char *inner_key,
                      Table *result, JoinComparison *cmp) {
    if (!outer || !inner || !result) return -1;

    int oc = table_find_col(outer, outer_key);
    int ic = table_find_col(inner, inner_key);

    double compare_ops = 0;
    int tuples_out = 0;

    for (int i = 0; i < outer->num_rows; i++) {
        if (outer->rows[i].is_deleted) continue;
        for (int j = 0; j < inner->num_rows; j++) {
            if (inner->rows[j].is_deleted) continue;
            compare_ops += 1.0;
            if (oc >= 0 && ic >= 0) {
                if (strcmp(outer->rows[i].values[oc],
                           inner->rows[j].values[ic]) == 0) {
                    Row combined;
                    memset(&combined, 0, sizeof(Row));
                    combined.num_fields = outer->rows[i].num_fields + inner->rows[j].num_fields;
                    for (int f = 0; f < outer->rows[i].num_fields; f++)
                        strcpy(combined.values[f], outer->rows[i].values[f]);
                    for (int f = 0; f < inner->rows[j].num_fields; f++)
                        strcpy(combined.values[f + outer->rows[i].num_fields],
                               inner->rows[j].values[f]);
                    if (result->num_rows < TABLE_MAX_ROWS) {
                        result->rows[result->num_rows++] = combined;
                        tuples_out++;
                    }
                }
            }
        }
    }

    if (cmp) {
        cmp->type = JOIN_NESTED_LOOP;
        cmp->rows_outer = outer->num_rows;
        cmp->rows_inner = inner->num_rows;
        cmp->compare_ops = compare_ops;
        cmp->hash_ops = 0;
        cmp->io_pages = outer->num_rows * inner->num_rows * 0.01;
        snprintf(cmp->description, sizeof(cmp->description),
                 "Nested Loop Join: outer=%.0f inner=%.0f compares=%.0f result=%d",
                 cmp->rows_outer, cmp->rows_inner, cmp->compare_ops, tuples_out);
    }
    return tuples_out;
}

int join_sort_merge(Table *outer, Table *inner,
                     const char *outer_key, const char *inner_key,
                     Table *result, JoinComparison *cmp) {
    if (!outer || !inner || !result) return -1;

    int oc = table_find_col(outer, outer_key);
    int ic = table_find_col(inner, inner_key);
    if (oc < 0 || ic < 0) return -1;

    Table outer_sorted = *outer;
    Table inner_sorted = *inner;
    table_sort(&outer_sorted, oc);
    table_sort(&inner_sorted, ic);

    double compare_ops = 0;
    int tuples_out = 0;
    int o = 0, in = 0;

    while (o < outer_sorted.num_rows && in < inner_sorted.num_rows) {
        if (outer_sorted.rows[o].is_deleted) { o++; continue; }
        if (inner_sorted.rows[in].is_deleted) { in++; continue; }

        compare_ops += 1.0;
        int c = strcmp(outer_sorted.rows[o].values[oc],
                        inner_sorted.rows[in].values[ic]);
        if (c < 0) {
            o++;
        } else if (c > 0) {
            in++;
        } else {
            Row combined;
            memset(&combined, 0, sizeof(Row));
            combined.num_fields = outer_sorted.rows[o].num_fields + inner_sorted.rows[in].num_fields;
            for (int f = 0; f < outer_sorted.rows[o].num_fields; f++)
                strcpy(combined.values[f], outer_sorted.rows[o].values[f]);
            for (int f = 0; f < inner_sorted.rows[in].num_fields; f++)
                strcpy(combined.values[f + outer_sorted.rows[o].num_fields],
                       inner_sorted.rows[in].values[f]);
            if (result->num_rows < TABLE_MAX_ROWS) {
                result->rows[result->num_rows++] = combined;
                tuples_out++;
            }
            in++;
        }
    }

    if (cmp) {
        cmp->type = JOIN_SORT_MERGE;
        cmp->rows_outer = outer->num_rows;
        cmp->rows_inner = inner->num_rows;
        cmp->compare_ops = compare_ops;
        cmp->hash_ops = 0;
        double sort_cost = outer->num_rows * log(outer->num_rows > 1 ? (double)outer->num_rows : 2.0)
                         + inner->num_rows * log(inner->num_rows > 1 ? (double)inner->num_rows : 2.0);
        cmp->io_pages = (outer->num_rows + inner->num_rows) * 0.02 + sort_cost * 0.1;
        snprintf(cmp->description, sizeof(cmp->description),
                 "Sort-Merge Join: outer=%.0f inner=%.0f compares=%.0f result=%d",
                 cmp->rows_outer, cmp->rows_inner, cmp->compare_ops, tuples_out);
    }
    return tuples_out;
}

void join_compare_all(Table *outer, Table *inner,
                       const char *outer_key, const char *inner_key,
                       JoinComparison out[4]) {
    memset(out, 0, 4 * sizeof(JoinComparison));

    Table *result = table_create("join_result", 0, NULL);
    HashTable ht;
    join_hash_build(&ht, inner, inner_key);
    result->num_rows = 0;
    join_hash_probe(&ht, outer, outer_key, result, &out[0]);
    hash_table_destroy(&ht);
    table_free(result);

    result = table_create("join_result", 0, NULL);
    join_nested_loop(outer, inner, outer_key, inner_key, result, &out[1]);
    table_free(result);

    result = table_create("join_result", 0, NULL);
    join_sort_merge(outer, inner, outer_key, inner_key, result, &out[2]);
    table_free(result);

    out[3] = out[0];
    out[3].type = JOIN_GRACE_HASH;
    snprintf(out[3].description, sizeof(out[3].description),
             "GRACE Hash Join: same as hash, partitions=%d", 4);
}

void join_print_comparison(const JoinComparison *cmp) {
    if (!cmp) return;
    const char *names[] = { "Hash Join", "Nested Loop", "Sort-Merge", "GRACE Hash" };
    printf("[%s]\n", cmp->type < 4 ? names[cmp->type] : "Unknown");
    printf("  %s\n", cmp->description);
    printf("  compares=%.0f  hash_ops=%.0f  io_pages=%.2f\n",
           cmp->compare_ops, cmp->hash_ops, cmp->io_pages);
}
