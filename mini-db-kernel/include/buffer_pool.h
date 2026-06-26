#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BP_PAGE_SIZE      4096
#define BP_CAPACITY       1024
#define BP_HASH_BUCKETS   2048

typedef struct Frame {
    int32_t  page_id;
    int32_t  pin_count;
    bool     dirty;
    uint8_t  data[BP_PAGE_SIZE];
} Frame;

typedef struct PageTableEntry {
    int32_t page_id;
    int32_t frame_idx;
    struct PageTableEntry *next;
} PageTableEntry;

typedef struct LRUNode {
    int32_t frame_idx;
    struct LRUNode *prev;
    struct LRUNode *next;
} LRUNode;

typedef struct BufferPool {
    Frame          frames[BP_CAPACITY];
    bool           occupied[BP_CAPACITY];
    PageTableEntry *page_table[BP_HASH_BUCKETS];
    LRUNode        *lru_head;
    LRUNode        *lru_tail;
    int32_t        capacity;
    int32_t        num_pages;
    int32_t        clock_hand;
    int64_t        hits;
    int64_t        misses;
} BufferPool;

void  bp_init(BufferPool *bp);
int32_t bp_fetch_page(BufferPool *bp, int32_t page_id);
void    bp_unpin_page(BufferPool *bp, int32_t page_id);
void    bp_flush_page(BufferPool *bp, int32_t page_id);
void    bp_flush_all(BufferPool *bp);
void    bp_print_stats(BufferPool *bp);

#endif
