#include "buffer_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void lru_remove(BufferPool *bp, int32_t frame_idx) {
    LRUNode *curr = bp->lru_head;
    while (curr) {
        if (curr->frame_idx == frame_idx) {
            if (curr->prev) curr->prev->next = curr->next;
            else bp->lru_head = curr->next;
            if (curr->next) curr->next->prev = curr->prev;
            else bp->lru_tail = curr->prev;
            free(curr);
            return;
        }
        curr = curr->next;
    }
}

static void lru_touch(BufferPool *bp, int32_t frame_idx) {
    lru_remove(bp, frame_idx);
    LRUNode *node = malloc(sizeof(LRUNode));
    node->frame_idx = frame_idx;
    node->prev = NULL;
    node->next = bp->lru_head;
    if (bp->lru_head) bp->lru_head->prev = node;
    bp->lru_head = node;
    if (!bp->lru_tail) bp->lru_tail = node;
}

static int32_t lru_evict(BufferPool *bp) {
    if (!bp->lru_tail) return -1;
    LRUNode *node = bp->lru_tail;
    while (node) {
        int32_t idx = node->frame_idx;
        if (bp->frames[idx].pin_count == 0) {
            if (bp->frames[idx].dirty) {
                bp_flush_page(bp, bp->frames[idx].page_id);
            }
            int32_t result = idx;
            lru_remove(bp, idx);
            return result;
        }
        node = node->prev;
    }
    return -1;
}

static uint32_t hash_page_id(int32_t page_id) {
    uint32_t h = (uint32_t)page_id * 2654435761ULL;
    return h % BP_HASH_BUCKETS;
}

static int32_t page_table_lookup(BufferPool *bp, int32_t page_id) {
    uint32_t bucket = hash_page_id(page_id);
    PageTableEntry *entry = bp->page_table[bucket];
    while (entry) {
        if (entry->page_id == page_id) return entry->frame_idx;
        entry = entry->next;
    }
    return -1;
}

static void page_table_insert(BufferPool *bp, int32_t page_id, int32_t frame_idx) {
    uint32_t bucket = hash_page_id(page_id);
    PageTableEntry *entry = malloc(sizeof(PageTableEntry));
    entry->page_id = page_id;
    entry->frame_idx = frame_idx;
    entry->next = bp->page_table[bucket];
    bp->page_table[bucket] = entry;
}

static void page_table_remove(BufferPool *bp, int32_t page_id) {
    uint32_t bucket = hash_page_id(page_id);
    PageTableEntry *entry = bp->page_table[bucket];
    PageTableEntry *prev = NULL;
    while (entry) {
        if (entry->page_id == page_id) {
            if (prev) prev->next = entry->next;
            else bp->page_table[bucket] = entry->next;
            free(entry);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

void bp_init(BufferPool *bp) {
    memset(bp, 0, sizeof(*bp));
    bp->capacity = BP_CAPACITY;
}

int32_t bp_fetch_page(BufferPool *bp, int32_t page_id) {
    int32_t frame_idx = page_table_lookup(bp, page_id);
    if (frame_idx >= 0) {
        bp->frames[frame_idx].pin_count++;
        lru_touch(bp, frame_idx);
        bp->hits++;
        return frame_idx;
    }
    bp->misses++;
    if (bp->num_pages < bp->capacity) {
        frame_idx = bp->num_pages;
        bp->occupied[frame_idx] = true;
        bp->frames[frame_idx].page_id = page_id;
        bp->frames[frame_idx].pin_count = 1;
        bp->frames[frame_idx].dirty = false;
        memset(bp->frames[frame_idx].data, 0, BP_PAGE_SIZE);
        bp->num_pages++;
    } else {
        frame_idx = lru_evict(bp);
        if (frame_idx < 0) {
            for (int32_t i = 0; i < bp->capacity; i++) {
                if (bp->frames[i].pin_count == 0) {
                    frame_idx = i;
                    break;
                }
            }
            if (frame_idx < 0) return -1;
        }
        page_table_remove(bp, bp->frames[frame_idx].page_id);
        bp->frames[frame_idx].page_id = page_id;
        bp->frames[frame_idx].pin_count = 1;
        bp->frames[frame_idx].dirty = false;
        memset(bp->frames[frame_idx].data, 0, BP_PAGE_SIZE);
    }
    page_table_insert(bp, page_id, frame_idx);
    lru_touch(bp, frame_idx);
    return frame_idx;
}

void bp_unpin_page(BufferPool *bp, int32_t page_id) {
    int32_t frame_idx = page_table_lookup(bp, page_id);
    if (frame_idx >= 0 && bp->frames[frame_idx].pin_count > 0) {
        bp->frames[frame_idx].pin_count--;
    }
}

void bp_flush_page(BufferPool *bp, int32_t page_id) {
    int32_t frame_idx = page_table_lookup(bp, page_id);
    if (frame_idx >= 0 && bp->frames[frame_idx].dirty) {
        bp->frames[frame_idx].dirty = false;
    }
}

void bp_flush_all(BufferPool *bp) {
    for (int32_t i = 0; i < bp->capacity; i++) {
        if (bp->occupied[i] && bp->frames[i].dirty) {
            bp->frames[i].dirty = false;
        }
    }
}

void bp_print_stats(BufferPool *bp) {
    printf("Buffer Pool Stats:\n");
    printf("  Pages: %d / %d\n", bp->num_pages, bp->capacity);
    printf("  Hits:  %lld\n", (long long)bp->hits);
    printf("  Misses: %lld\n", (long long)bp->misses);
    if (bp->hits + bp->misses > 0) {
        double ratio = (double)bp->hits / (double)(bp->hits + bp->misses) * 100.0;
        printf("  Hit Ratio: %.2f%%\n", ratio);
    }
    int32_t dirty_count = 0;
    int32_t pinned_count = 0;
    for (int32_t i = 0; i < bp->capacity; i++) {
        if (bp->occupied[i]) {
            if (bp->frames[i].dirty) dirty_count++;
            if (bp->frames[i].pin_count > 0) pinned_count++;
        }
    }
    printf("  Dirty: %d\n", dirty_count);
    printf("  Pinned: %d\n", pinned_count);
}
