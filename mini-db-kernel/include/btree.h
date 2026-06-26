#ifndef BTREE_H
#define BTREE_H

#include <stdbool.h>
#include <stdint.h>

#define BTREE_ORDER 5

typedef struct BTreeNode {
    bool    is_leaf;
    int32_t keys[BTREE_ORDER];
    int32_t children[BTREE_ORDER];
    int32_t values[BTREE_ORDER];
    int32_t num_keys;
    int32_t page_id;
    int32_t next_leaf;
} BTreeNode;

typedef BTreeNode* BTree;

BTree     btree_create(void);
bool      btree_insert(BTree *root, int32_t key, int32_t value);
bool      btree_search(BTree root, int32_t key, int32_t *value);
int32_t   btree_search_range(BTree root, int32_t low, int32_t high,
                             int32_t *keys_out, int32_t *values_out, int32_t max_results);
bool      btree_delete(BTree *root, int32_t key);
void      btree_print_tree(BTree root);

#endif
