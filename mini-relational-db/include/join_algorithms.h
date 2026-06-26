#ifndef JOIN_ALGORITHMS_H
#define JOIN_ALGORITHMS_H

#include "row_store.h"
#include <stddef.h>

#define JOIN_HT_SIZE 256
#define JOIN_MAX_TUPLES 1024

typedef enum {
    JOIN_HASH         = 0,
    JOIN_NESTED_LOOP  = 1,
    JOIN_SORT_MERGE   = 2,
    JOIN_GRACE_HASH   = 3
} JoinType;

typedef struct JoinTuple {
    Row     row;
    struct JoinTuple *next;
} JoinTuple;

typedef struct {
    JoinTuple *head;
    JoinTuple *tail;
    int   count;
} JoinBucket;

typedef struct {
    JoinBucket buckets[JOIN_HT_SIZE];
    int        key_col;
} HashTable;

typedef struct {
    JoinType type;
    double   rows_outer;
    double   rows_inner;
    double   compare_ops;
    double   hash_ops;
    double   io_pages;
    char     description[256];
} JoinComparison;

void hash_table_init(HashTable *ht, int key_col);
void hash_table_insert(HashTable *ht, const Row *row);
int  hash_table_lookup(HashTable *ht, const char *key, JoinTuple **result);
void hash_table_destroy(HashTable *ht);

int join_hash_build(HashTable *ht, Table *inner, const char *inner_key);
int join_hash_probe(HashTable *ht, Table *outer, const char *outer_key,
                     Table *result, JoinComparison *cmp);
int join_nested_loop(Table *outer, Table *inner,
                      const char *outer_key, const char *inner_key,
                      Table *result, JoinComparison *cmp);
int join_sort_merge(Table *outer, Table *inner,
                     const char *outer_key, const char *inner_key,
                     Table *result, JoinComparison *cmp);

void join_compare_all(Table *outer, Table *inner,
                       const char *outer_key, const char *inner_key,
                       JoinComparison out[4]);
void join_print_comparison(const JoinComparison *cmp);

#endif
