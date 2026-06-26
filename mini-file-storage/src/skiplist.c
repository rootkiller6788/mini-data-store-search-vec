#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "skiplist.h"

/* ─────────────────────────────────────────────
   Random level generator
   Uses: while rand() < 0.5 and level < MAX_LEVEL
   ───────────────────────────────────────────── */
static int g_random_seeded = 0;

static uint32_t random_level(void) {
    if (!g_random_seeded) {
        srand((unsigned int)time(NULL));
        g_random_seeded = 1;
    }
    uint32_t level = 0;
    while ((rand() / (double)RAND_MAX) < 0.5 && level < SKIPLIST_MAX_LEVEL - 1)
        level++;
    return level;
}

/* ─────────────────────────────────────────────
   Create a new skip list
   ───────────────────────────────────────────── */
SkipList *skiplist_create(void) {
    SkipList *list = (SkipList *)malloc(sizeof(SkipList));
    if (!list) return NULL;

    list->header = (SkipNode *)calloc(1, sizeof(SkipNode));
    if (!list->header) { free(list); return NULL; }
    list->header->key_len = 0;
    list->header->value_len = 0;

    list->level = 0;
    list->size = 0;
    return list;
}

/* ─────────────────────────────────────────────
   Insert a key-value pair
   ───────────────────────────────────────────── */
int skiplist_insert(SkipList *list,
                    const uint8_t *key, uint32_t key_len,
                    const uint8_t *value, uint32_t value_len) {
    if (!list || !key || key_len > 255 || value_len > 1023) return -1;

    SkipNode *update[SKIPLIST_MAX_LEVEL];
    SkipNode *x = list->header;

    /* Find insertion points at each level */
    for (int i = (int)list->level; i >= 0; i--) {
        while (x->forward[i] && x->forward[i]->key_len > 0) {
            uint32_t cmp_len = x->forward[i]->key_len < key_len
                ? x->forward[i]->key_len : key_len;
            int cmp = memcmp(x->forward[i]->key, key, cmp_len);
            if (cmp == 0 && x->forward[i]->key_len != key_len)
                cmp = x->forward[i]->key_len < key_len ? -1 : 1;
            if (cmp < 0) x = x->forward[i];
            else break;
        }
        update[i] = x;
    }

    /* Check for duplicate key and update value if found */
    x = x->forward[0];
    if (x && x->key_len == key_len && memcmp(x->key, key, key_len) == 0) {
        memcpy(x->value, value, value_len);
        x->value_len = value_len;
        return 0;
    }

    uint32_t new_level = random_level();
    if (new_level > list->level) {
        for (uint32_t i = list->level + 1; i <= new_level; i++)
            update[i] = list->header;
        list->level = new_level;
    }

    SkipNode *node = (SkipNode *)calloc(1, sizeof(SkipNode));
    if (!node) return -1;
    memcpy(node->key, key, key_len);
    node->key_len = key_len;
    memcpy(node->value, value, value_len);
    node->value_len = value_len;

    for (uint32_t i = 0; i <= new_level; i++) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }
    list->size++;
    return 0;
}

/* ─────────────────────────────────────────────
   Search for a key, return node or NULL
   ───────────────────────────────────────────── */
SkipNode *skiplist_search(SkipList *list,
                          const uint8_t *key, uint32_t key_len) {
    if (!list || !key) return NULL;

    SkipNode *x = list->header;
    for (int i = (int)list->level; i >= 0; i--) {
        while (x->forward[i] && x->forward[i]->key_len > 0) {
            uint32_t cmp_len = x->forward[i]->key_len < key_len
                ? x->forward[i]->key_len : key_len;
            int cmp = memcmp(x->forward[i]->key, key, cmp_len);
            if (cmp == 0 && x->forward[i]->key_len != key_len)
                cmp = x->forward[i]->key_len < key_len ? -1 : 1;
            if (cmp < 0) x = x->forward[i];
            else break;
        }
    }
    x = x->forward[0];
    if (x && x->key_len == key_len && memcmp(x->key, key, key_len) == 0)
        return x;
    return NULL;
}

/* ─────────────────────────────────────────────
   Create an iterator (points to first element)
   ───────────────────────────────────────────── */
SkipListIterator *skiplist_iterator(SkipList *list) {
    if (!list) return NULL;
    SkipListIterator *iter = (SkipListIterator *)malloc(sizeof(SkipListIterator));
    if (!iter) return NULL;
    iter->list = list;
    iter->current = list->header; /* before first */
    return iter;
}

/* ─────────────────────────────────────────────
   Advance iterator to next node
   ───────────────────────────────────────────── */
SkipNode *skiplist_iterator_next(SkipListIterator *iter) {
    if (!iter) return NULL;
    iter->current = iter->current->forward[0];
    /* header node has key_len == 0, skip it */
    if (iter->current && iter->current->key_len == 0)
        iter->current = iter->current->forward[0];
    return iter->current;
}

/* ─────────────────────────────────────────────
   Destroy iterator
   ───────────────────────────────────────────── */
void skiplist_iterator_destroy(SkipListIterator *iter) {
    free(iter);
}

/* ─────────────────────────────────────────────
   Destroy the entire skip list
   ───────────────────────────────────────────── */
void skiplist_destroy(SkipList *list) {
    if (!list) return;
    SkipNode *node = list->header->forward[0];
    while (node) {
        SkipNode *next = node->forward[0];
        free(node);
        node = next;
    }
    free(list->header);
    free(list);
}
