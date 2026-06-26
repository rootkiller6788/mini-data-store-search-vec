#ifndef BPTREE_H
#define BPTREE_H

#include <stddef.h>

/*
 * B+Tree Index — L5 Algorithm: Multi-way balanced search tree.
 *
 * Theorem (Bayer & McCreight, 1972):
 *   For a B+Tree of order m, height h is bounded by:
 *     h <= ceil(log_{ceil(m/2)}((n+1)/2))
 *   where n is the number of keys.
 *
 * Search complexity: O(log_m n)  — fanout reduces height.
 * Insert complexity: O(m * log_m n) — may split nodes up to root.
 * Range query: O(log_m n + k) where k is result size.
 */

#define BP_MAX_KEYS     4
#define BP_MAX_CHILDREN 5
#define BP_KEY_SIZE    64
#define BP_VAL_SIZE    64

typedef enum {
    BP_NODE_INTERNAL = 0,
    BP_NODE_LEAF     = 1
} BPNodeType;

typedef struct {
    char key[BP_KEY_SIZE];
    char val[BP_VAL_SIZE];
} BPEntry;

typedef struct BPNode BPNode;

struct BPNode {
    BPNodeType type;
    int        num_keys;
    char       keys[BP_MAX_KEYS][BP_KEY_SIZE];
    BPNode    *children[BP_MAX_CHILDREN];
    BPEntry    entries[BP_MAX_KEYS];
    BPNode    *next;
};

typedef struct {
    BPNode *root;
    int     count;
} BPTree;

BPTree *bptree_create(void);
void    bptree_destroy(BPTree *tree);
int     bptree_insert(BPTree *tree, const char *key, const char *val);
int     bptree_search(const BPTree *tree, const char *key, char *val_out);
int     bptree_range_scan(const BPTree *tree, const char *low, const char *high,
                           void (*cb)(const char *key, const char *val, void *ctx),
                           void *ctx);
int     bptree_delete(BPTree *tree, const char *key);
void    bptree_dump(const BPTree *tree);

#endif
