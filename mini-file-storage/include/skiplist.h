#ifndef MINI_SKIPLIST_H
#define MINI_SKIPLIST_H

#include <stdint.h>
#include <stddef.h>

#define SKIPLIST_MAX_LEVEL 12

/* ─────────────────────────────────────────────
   Skip List Node
   ───────────────────────────────────────────── */
typedef struct SkipNode {
    uint8_t           key[256];
    uint32_t          key_len;
    uint8_t           value[1024];
    uint32_t          value_len;
    struct SkipNode  *forward[SKIPLIST_MAX_LEVEL];
} SkipNode;

/* ─────────────────────────────────────────────
   Skip List Header
   ───────────────────────────────────────────── */
typedef struct {
    SkipNode *header;
    uint32_t  level;      /* current highest level (0-indexed) */
    uint32_t  size;       /* number of elements               */
} SkipList;

/* ─────────────────────────────────────────────
   Iterator for in-order traversal
   ───────────────────────────────────────────── */
typedef struct {
    SkipList  *list;
    SkipNode  *current;
} SkipListIterator;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

SkipList       *skiplist_create(void);
int             skiplist_insert(SkipList *list,
                                const uint8_t *key, uint32_t key_len,
                                const uint8_t *value, uint32_t value_len);
SkipNode       *skiplist_search(SkipList *list,
                                const uint8_t *key, uint32_t key_len);
SkipListIterator *skiplist_iterator(SkipList *list);
SkipNode        *skiplist_iterator_next(SkipListIterator *iter);
void             skiplist_iterator_destroy(SkipListIterator *iter);
void             skiplist_destroy(SkipList *list);

#endif /* MINI_SKIPLIST_H */
