#include "bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * B+Tree Implementation - L5 Algorithm
 * Reference: CMU 15-445 Lecture 7: B+Tree Indexes
 *
 * This implementation uses a straightforward recursive insert with
 * bottom-up split propagation. All nodes maintain sorted key order.
 */

static BPNode *node_alloc(BPNodeType type) {
    BPNode *n = calloc(1, sizeof(BPNode));
    if (!n) { fprintf(stderr, "bptree: OOM\n"); exit(1); }
    n->type = type;
    return n;
}

static void node_free_subtree(BPNode *n) {
    if (!n) return;
    if (n->type == BP_NODE_INTERNAL) {
        for (int i = 0; i <= n->num_keys; i++)
            node_free_subtree(n->children[i]);
    }
    free(n);
}

BPTree *bptree_create(void) {
    BPTree *t = malloc(sizeof(BPTree));
    if (!t) { fprintf(stderr, "bptree_create: OOM\n"); exit(1); }
    t->root = node_alloc(BP_NODE_LEAF);
    t->count = 0;
    return t;
}

void bptree_destroy(BPTree *tree) {
    if (!tree) return;
    node_free_subtree(tree->root);
    free(tree);
}

/*
 * Find position in leaf entries using binary search.
 * Returns index where key should be (for insert) or where it IS (for search).
 * Standard lower_bound: first i where entries[i].key >= key.
 */
static int leaf_find(const BPEntry *entries, int n, const char *key) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(entries[mid].key, key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/*
 * Find child index in internal node.
 * Returns i such that children[i] is the subtree containing key.
 * Uses: while (i < n && key >= keys[i]) i++;
 */
static int internal_find_child(const char keys[][BP_KEY_SIZE], int n,
                                const char *key) {
    int i = 0;
    while (i < n && strcmp(key, keys[i]) >= 0)
        i++;
    return i;
}

/*
 * Insert into a leaf that has room (called after overflow check).
 */
static void leaf_insert_at(BPNode *leaf, int pos, const char *key,
                            const char *val) {
    for (int i = leaf->num_keys; i > pos; i--)
        leaf->entries[i] = leaf->entries[i - 1];
    strcpy(leaf->entries[pos].key, key);
    strcpy(leaf->entries[pos].val, val);
    leaf->num_keys++;
}

/*
 * Insert key+child into an internal node at given position.
 * pos is the index where the new child goes (as children[pos+1]),
 * and key goes to keys[pos].
 */
static void internal_insert_at(BPNode *node, int pos, const char *key,
                                BPNode *child) {
    for (int i = node->num_keys; i > pos; i--) {
        strcpy(node->keys[i], node->keys[i - 1]);
        node->children[i + 1] = node->children[i];
    }
    node->children[pos + 1] = child;
    strcpy(node->keys[pos], key);
    node->num_keys++;
}

/*
 * Recursive insert. Returns:
 *   0 = done (no split)
 *   1 = node split, *sep and *new_node set
 *  -1 = duplicate key
 */
static int insert_rec(BPNode *node, const char *key, const char *val,
                       char *sep, BPNode **new_node) {
    if (node->type == BP_NODE_LEAF) {
        /* Find insertion point */
        int pos = leaf_find(node->entries, node->num_keys, key);

        /* Check duplicate */
        if (pos < node->num_keys &&
            strcmp(node->entries[pos].key, key) == 0)
            return -1;

        if (node->num_keys < BP_MAX_KEYS) {
            leaf_insert_at(node, pos, key, val);
            return 0;
        }

        /* Leaf is full. Build overflow array and split. */
        BPEntry overflow[BP_MAX_KEYS + 1];
        int j = 0;
        for (int i = 0; i < pos; i++)
            overflow[j++] = node->entries[i];
        strcpy(overflow[j].key, key);
        strcpy(overflow[j].val, val);
        j++;
        for (int i = pos; i < node->num_keys; i++)
            overflow[j++] = node->entries[i];

        /* Place overflow entries back into node temporarily */
        int split_pt = (BP_MAX_KEYS + 1) / 2; /* 2 */

        BPNode *right = node_alloc(BP_NODE_LEAF);
        for (int i = 0; i < split_pt; i++)
            node->entries[i] = overflow[i];
        node->num_keys = split_pt;

        for (int i = split_pt; i <= BP_MAX_KEYS; i++) {
            right->entries[i - split_pt] = overflow[i];
            right->num_keys++;
        }

        right->next = node->next;
        node->next = right;

        strcpy(sep, right->entries[0].key);
        *new_node = right;
        return 1;
    }

    /* Internal node */
    int child_idx = internal_find_child(node->keys, node->num_keys, key);
    BPNode *child = node->children[child_idx];

    char child_sep[BP_KEY_SIZE];
    BPNode *child_new = NULL;
    int rc = insert_rec(child, key, val, child_sep, &child_new);

    if (rc <= 0) return rc;

    /* Child split - need to insert separator and new child */
    if (node->num_keys < BP_MAX_KEYS) {
        internal_insert_at(node, child_idx, child_sep, child_new);
        return 0;
    }

    /* Internal node also full - build overflow and split */
    char    ov_keys[BP_MAX_KEYS + 1][BP_KEY_SIZE];
    BPNode *ov_children[BP_MAX_CHILDREN + 1];
    int total_keys = node->num_keys + 1;
    int j = 0, k = 0;

    /* Copy children[0..child_idx] (includes the left half of split child) */
    for (int i = 0; i <= child_idx; i++)
        ov_children[k++] = node->children[i];
    /* New right half child */
    ov_children[k++] = child_new;
    /* Copy remaining children[child_idx+1..num_keys] */
    for (int i = child_idx + 1; i <= node->num_keys; i++)
        ov_children[k++] = node->children[i];

    /* Copy keys[0..child_idx-1] */
    for (int i = 0; i < child_idx; i++)
        strcpy(ov_keys[j++], node->keys[i]);
    /* Insert separator */
    strcpy(ov_keys[j++], child_sep);
    /* Copy remaining keys[child_idx..num_keys-1] */
    for (int i = child_idx; i < node->num_keys; i++)
        strcpy(ov_keys[j++], node->keys[i]);

    /* Step 2: split */
    int mid = total_keys / 2; /* separator index to promote */

    BPNode *new_right = node_alloc(BP_NODE_INTERNAL);

    /* Left half (this node gets keys[0..mid-1] and children[0..mid]) */
    for (int i = 0; i < mid; i++) {
        strcpy(node->keys[i], ov_keys[i]);
        node->children[i] = ov_children[i];
    }
    node->children[mid] = ov_children[mid];
    node->num_keys = mid;

    /* Right half */
    for (int i = mid + 1; i < total_keys; i++) {
        strcpy(new_right->keys[i - mid - 1], ov_keys[i]);
        new_right->children[i - mid - 1] = ov_children[i];
        new_right->num_keys++;
    }
    new_right->children[new_right->num_keys] = ov_children[total_keys];

    /* Promote middle key */
    strcpy(sep, ov_keys[mid]);
    *new_node = new_right;
    return 1;
}

int bptree_insert(BPTree *tree, const char *key, const char *val) {
    if (!tree || !key || !val) return -2;

    char sep[BP_KEY_SIZE];
    BPNode *new_child = NULL;
    int rc = insert_rec(tree->root, key, val, sep, &new_child);

    if (rc < 0) return rc;

    if (rc == 1) {
        /* Root split - create new root */
        BPNode *new_root = node_alloc(BP_NODE_INTERNAL);
        strcpy(new_root->keys[0], sep);
        new_root->children[0] = tree->root;
        new_root->children[1] = new_child;
        new_root->num_keys = 1;
        tree->root = new_root;
    }

    tree->count++;
    return 0;
}

int bptree_search(const BPTree *tree, const char *key, char *val_out) {
    if (!tree || !key) return 0;

    BPNode *n = tree->root;
    while (n->type == BP_NODE_INTERNAL) {
        int c = internal_find_child(n->keys, n->num_keys, key);
        n = n->children[c];
    }

    int pos = leaf_find(n->entries, n->num_keys, key);
    if (pos < n->num_keys && strcmp(n->entries[pos].key, key) == 0) {
        if (val_out) strcpy(val_out, n->entries[pos].val);
        return 1;
    }
    return 0;
}

int bptree_range_scan(const BPTree *tree, const char *low, const char *high,
                       void (*cb)(const char *key, const char *val, void *ctx),
                       void *ctx) {
    if (!tree || !low || !high || !cb) return 0;

    BPNode *n = tree->root;
    while (n->type == BP_NODE_INTERNAL) {
        int c = internal_find_child(n->keys, n->num_keys, low);
        n = n->children[c];
    }

    int count = 0;
    while (n) {
        for (int i = 0; i < n->num_keys; i++) {
            const char *k = n->entries[i].key;
            if (strcmp(k, low) >= 0 && strcmp(k, high) <= 0) {
                cb(k, n->entries[i].val, ctx);
                count++;
            }
            if (strcmp(k, high) > 0) return count;
        }
        n = n->next;
    }
    return count;
}

int bptree_delete(BPTree *tree, const char *key) {
    if (!tree || !key) return -1;

    BPNode *n = tree->root;
    while (n->type == BP_NODE_INTERNAL) {
        int c = internal_find_child(n->keys, n->num_keys, key);
        n = n->children[c];
    }

    int found = -1;
    for (int i = 0; i < n->num_keys; i++) {
        if (strcmp(n->entries[i].key, key) == 0) { found = i; break; }
    }
    if (found < 0) return -1;

    for (int i = found; i < n->num_keys - 1; i++)
        n->entries[i] = n->entries[i + 1];
    n->num_keys--;
    tree->count--;

    if (tree->root->type == BP_NODE_INTERNAL && tree->root->num_keys == 0) {
        BPNode *old = tree->root;
        tree->root = old->children[0];
        free(old);
    }
    return 0;
}

static void node_dump(const BPNode *n, int depth) {
    for (int d = 0; d < depth; d++) printf("  ");
    if (n->type == BP_NODE_INTERNAL) {
        printf("[Internal: %d keys]", n->num_keys);
        for (int i = 0; i < n->num_keys; i++)
            printf(" %s", n->keys[i]);
        printf("\n");
        for (int i = 0; i <= n->num_keys; i++)
            node_dump(n->children[i], depth + 1);
    } else {
        printf("[Leaf: %d keys]", n->num_keys);
        for (int i = 0; i < n->num_keys; i++)
            printf(" (%s->%s)", n->entries[i].key, n->entries[i].val);
        if (n->next) printf(" ->");
        printf("\n");
    }
}

void bptree_dump(const BPTree *tree) {
    if (!tree) { printf("BPTree: (null)\n"); return; }
    printf("BPTree: %d entries\n", tree->count);
    node_dump(tree->root, 0);
}
