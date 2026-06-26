#include "btree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int32_t next_page_id = 0;

static BTreeNode *node_create(bool is_leaf) {
    BTreeNode *node = malloc(sizeof(BTreeNode));
    memset(node, 0, sizeof(*node));
    node->is_leaf = is_leaf;
    node->num_keys = 0;
    node->page_id = next_page_id++;
    node->next_leaf = -1;
    return node;
}

BTree btree_create(void) {
    BTreeNode *root = node_create(true);
    return root;
}

static void insert_nonfull(BTreeNode *node, int32_t key, int32_t value) {
    int32_t i = node->num_keys - 1;
    if (node->is_leaf) {
        while (i >= 0 && node->keys[i] > key) {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->num_keys++;
    } else {
        while (i >= 0 && node->keys[i] > key) i--;
        i++;
        BTreeNode *child = (BTreeNode *)(intptr_t)node->children[i];
        if (!child) {
            child = node_create(true);
            node->children[i] = (int32_t)(intptr_t)child;
        }
        if (child->num_keys == BTREE_ORDER - 1) {
            BTreeNode *new_node = node_create(child->is_leaf);
            int32_t mid = (BTREE_ORDER - 1) / 2;
            new_node->num_keys = child->num_keys - mid - 1;
            for (int32_t j = 0; j < new_node->num_keys; j++) {
                new_node->keys[j] = child->keys[mid + 1 + j];
                new_node->values[j] = child->values[mid + 1 + j];
            }
            if (!child->is_leaf) {
                for (int32_t j = 0; j <= new_node->num_keys; j++) {
                    new_node->children[j] = child->children[mid + 1 + j];
                }
            } else {
                new_node->next_leaf = child->next_leaf;
                child->next_leaf = new_node->page_id;
            }
            child->num_keys = mid;
            for (int32_t j = node->num_keys; j > i; j--) {
                node->keys[j] = node->keys[j - 1];
                node->children[j + 1] = node->children[j];
            }
            node->keys[i] = child->keys[mid];
            node->children[i + 1] = (int32_t)(intptr_t)new_node;
            node->num_keys++;
            if (key > node->keys[i]) i++;
        }
        insert_nonfull(child, key, value);
    }
}

bool btree_insert(BTree *root, int32_t key, int32_t value) {
    if (*root == NULL) {
        *root = node_create(true);
    }
    BTreeNode *r = *root;
    if (r->num_keys == BTREE_ORDER - 1) {
        BTreeNode *new_root = node_create(false);
        new_root->children[0] = (int32_t)(intptr_t)r;
        BTreeNode *new_node = node_create(r->is_leaf);
        int32_t mid = (BTREE_ORDER - 1) / 2;
        new_node->num_keys = r->num_keys - mid - 1;
        for (int32_t j = 0; j < new_node->num_keys; j++) {
            new_node->keys[j] = r->keys[mid + 1 + j];
            new_node->values[j] = r->values[mid + 1 + j];
        }
        if (!r->is_leaf) {
            for (int32_t j = 0; j <= new_node->num_keys; j++) {
                new_node->children[j] = r->children[mid + 1 + j];
            }
        } else {
            new_node->next_leaf = r->next_leaf;
            r->next_leaf = new_node->page_id;
        }
        r->num_keys = mid;
        new_root->keys[0] = r->keys[mid];
        new_root->children[1] = (int32_t)(intptr_t)new_node;
        new_root->num_keys = 1;
        *root = new_root;
        if (key > new_root->keys[0]) {
            insert_nonfull(new_node, key, value);
        } else {
            insert_nonfull(r, key, value);
        }
    } else {
        insert_nonfull(r, key, value);
    }
    return true;
}

bool btree_search(BTree root, int32_t key, int32_t *value) {
    if (!root) return false;
    BTreeNode *node = root;
    while (node) {
        int32_t i = 0;
        while (i < node->num_keys && node->keys[i] < key) i++;
        if (node->is_leaf) {
            if (i < node->num_keys && node->keys[i] == key) {
                *value = node->values[i];
                return true;
            }
            return false;
        }
        if (i < node->num_keys && node->keys[i] == key) {
            *value = node->values[i];
            return true;
        }
        node = (BTreeNode *)(intptr_t)node->children[i];
    }
    return false;
}

static BTreeNode *find_leaf(BTree root, int32_t key) {
    if (!root) return NULL;
    BTreeNode *node = root;
    while (!node->is_leaf) {
        int32_t i = 0;
        while (i < node->num_keys && node->keys[i] <= key) i++;
        if (i == 0) i = 1;
        node = (BTreeNode *)(intptr_t)node->children[i - 1];
    }
    return node;
}

static BTreeNode *find_page(BTree root, int32_t page_id) {
    if (!root) return NULL;
    if (root->page_id == page_id) return root;
    BTreeNode *stack[256];
    int32_t top = 0;
    stack[top++] = root;
    while (top > 0) {
        BTreeNode *node = stack[--top];
        if (node->page_id == page_id) return node;
        if (!node->is_leaf) {
            for (int32_t i = 0; i <= node->num_keys; i++) {
                BTreeNode *child = (BTreeNode *)(intptr_t)node->children[i];
                if (child) {
                    if (child->page_id == page_id) return child;
                    stack[top++] = child;
                }
            }
        }
    }
    return NULL;
}

int32_t btree_search_range(BTree root, int32_t low, int32_t high,
                           int32_t *keys_out, int32_t *values_out, int32_t max_results) {
    if (!root) return 0;
    BTreeNode *leaf = find_leaf(root, low);
    int32_t count = 0;
    while (leaf && count < max_results) {
        for (int32_t i = 0; i < leaf->num_keys && count < max_results; i++) {
            if (leaf->keys[i] >= low && leaf->keys[i] <= high) {
                keys_out[count] = leaf->keys[i];
                values_out[count] = leaf->values[i];
                count++;
            }
            if (leaf->keys[i] > high) return count;
        }
        if (leaf->next_leaf >= 0) {
            leaf = find_page(root, leaf->next_leaf);
        } else {
            break;
        }
    }
    return count;
}

static void remove_from_node(BTreeNode *node, int32_t key) {
    int32_t idx = -1;
    for (int32_t i = 0; i < node->num_keys; i++) {
        if (node->keys[i] == key) { idx = i; break; }
    }
    if (idx < 0) return;
    for (int32_t i = idx; i < node->num_keys - 1; i++) {
        node->keys[i] = node->keys[i + 1];
        node->values[i] = node->values[i + 1];
    }
    node->num_keys--;
}

static bool delete_from_tree(BTreeNode *node, int32_t key) {
    int32_t idx = 0;
    while (idx < node->num_keys && node->keys[idx] < key) idx++;
    if (node->is_leaf) {
        if (idx < node->num_keys && node->keys[idx] == key) {
            remove_from_node(node, key);
            return true;
        }
        return false;
    }
    if (idx < node->num_keys && node->keys[idx] == key) {
        BTreeNode *left_child = (BTreeNode *)(intptr_t)node->children[idx];
        BTreeNode *right_child = (BTreeNode *)(intptr_t)node->children[idx + 1];
        if (left_child && left_child->num_keys > (BTREE_ORDER - 1) / 2) {
            int32_t pred = left_child->keys[left_child->num_keys - 1];
            node->keys[idx] = pred;
            return delete_from_tree(left_child, pred);
        } else if (right_child && right_child->num_keys > (BTREE_ORDER - 1) / 2) {
            int32_t succ = right_child->keys[0];
            node->keys[idx] = succ;
            return delete_from_tree(right_child, succ);
        } else if (left_child && right_child) {
            int32_t merged_key = node->keys[idx];
            left_child->keys[left_child->num_keys] = merged_key;
            left_child->values[left_child->num_keys] = node->values[idx];
            left_child->num_keys++;
            for (int32_t j = 0; j < right_child->num_keys; j++) {
                left_child->keys[left_child->num_keys + j] = right_child->keys[j];
                left_child->values[left_child->num_keys + j] = right_child->values[j];
            }
            left_child->num_keys += right_child->num_keys;
            if (!right_child->is_leaf) {
                for (int32_t j = 0; j <= right_child->num_keys; j++) {
                    left_child->children[left_child->num_keys - right_child->num_keys + j] = right_child->children[j];
                }
            }
            left_child->next_leaf = right_child->next_leaf;
            for (int32_t j = idx; j < node->num_keys - 1; j++) {
                node->keys[j] = node->keys[j + 1];
                node->values[j] = node->values[j + 1];
                node->children[j + 1] = node->children[j + 2];
            }
            node->num_keys--;
            free(right_child);
            return delete_from_tree(left_child, key);
        }
    } else {
        BTreeNode *child = (BTreeNode *)(intptr_t)node->children[idx];
        bool result = delete_from_tree(child, key);
        if (child->num_keys < (BTREE_ORDER - 1) / 2) {
            BTreeNode *left_sib = idx > 0 ? (BTreeNode *)(intptr_t)node->children[idx - 1] : NULL;
            BTreeNode *right_sib = idx < node->num_keys ? (BTreeNode *)(intptr_t)node->children[idx + 1] : NULL;
            if (left_sib && left_sib->num_keys > (BTREE_ORDER - 1) / 2) {
                for (int32_t j = child->num_keys; j > 0; j--) {
                    child->keys[j] = child->keys[j - 1];
                    child->values[j] = child->values[j - 1];
                }
                child->keys[0] = node->keys[idx - 1];
                child->values[0] = node->values[idx - 1];
                node->keys[idx - 1] = left_sib->keys[left_sib->num_keys - 1];
                child->num_keys++;
                if (!child->is_leaf) {
                    for (int32_t j = child->num_keys; j > 0; j--) {
                        child->children[j] = child->children[j - 1];
                    }
                    child->children[0] = left_sib->children[left_sib->num_keys];
                }
                left_sib->num_keys--;
            } else if (right_sib && right_sib->num_keys > (BTREE_ORDER - 1) / 2) {
                child->keys[child->num_keys] = node->keys[idx];
                child->values[child->num_keys] = node->values[idx];
                child->num_keys++;
                node->keys[idx] = right_sib->keys[0];
                if (!child->is_leaf) {
                    child->children[child->num_keys] = right_sib->children[0];
                }
                for (int32_t j = 0; j < right_sib->num_keys - 1; j++) {
                    right_sib->keys[j] = right_sib->keys[j + 1];
                    right_sib->values[j] = right_sib->values[j + 1];
                }
                if (!right_sib->is_leaf) {
                    for (int32_t j = 0; j < right_sib->num_keys; j++) {
                        right_sib->children[j] = right_sib->children[j + 1];
                    }
                }
                right_sib->num_keys--;
            } else if (left_sib) {
                left_sib->keys[left_sib->num_keys] = node->keys[idx - 1];
                left_sib->values[left_sib->num_keys] = node->values[idx - 1];
                left_sib->num_keys++;
                for (int32_t j = 0; j < child->num_keys; j++) {
                    left_sib->keys[left_sib->num_keys + j] = child->keys[j];
                    left_sib->values[left_sib->num_keys + j] = child->values[j];
                }
                left_sib->num_keys += child->num_keys;
                left_sib->next_leaf = child->next_leaf;
                for (int32_t j = idx - 1; j < node->num_keys - 1; j++) {
                    node->keys[j] = node->keys[j + 1];
                    node->values[j] = node->values[j + 1];
                    node->children[j + 1] = node->children[j + 2];
                }
                node->num_keys--;
                free(child);
            }
        }
        return result;
    }
}

bool btree_delete(BTree *root, int32_t key) {
    if (!*root) return false;
    bool result = delete_from_tree(*root, key);
    if ((*root)->num_keys == 0 && !(*root)->is_leaf) {
        BTreeNode *old = *root;
        *root = (BTreeNode *)(intptr_t)old->children[0];
        free(old);
    }
    return result;
}

static void print_node(BTreeNode *node, int32_t depth) {
    for (int32_t i = 0; i < depth; i++) printf("  ");
    printf("[page=%d leaf=%d]: ", node->page_id, node->is_leaf);
    for (int32_t i = 0; i < node->num_keys; i++) {
        printf("%d", node->keys[i]);
        if (node->is_leaf) printf(":%d", node->values[i]);
        if (i < node->num_keys - 1) printf(", ");
    }
    if (node->is_leaf && node->next_leaf >= 0) {
        printf(" -> leaf=%d", node->next_leaf);
    }
    printf("\n");
    if (!node->is_leaf) {
        for (int32_t i = 0; i <= node->num_keys; i++) {
            BTreeNode *child = (BTreeNode *)(intptr_t)node->children[i];
            if (child) print_node(child, depth + 1);
        }
    }
}

void btree_print_tree(BTree root) {
    if (!root) {
        printf("(empty tree)\n");
        return;
    }
    print_node(root, 0);
}
