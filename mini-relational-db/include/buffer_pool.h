#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Buffer Pool Manager — L3 Engineering Structure: LRU-based page cache.
 *
 * Reference: CMU 15-445 Lecture 4: Buffer Pools
 *
 * The buffer pool is the primary memory manager for page-oriented storage.
 * It caches disk pages in fixed-size frames, using LRU (Least Recently Used)
 * eviction to manage limited memory.
 *
 * Key concepts:
 *   - Frame: a memory slot holding one page
 *   - Page ID: unique identifier (file_id, page_number)
 *   - Pin count: prevent eviction of in-use pages
 *   - Dirty flag: track modified pages for write-back
 */

#define BP_MAX_FRAMES 64          /* max cached pages */
#define BP_PAGE_SIZE  4096        /* bytes per page */

/* Page identifier: (file_id, page_number) */
typedef struct {
    int file_id;
    int page_number;
} PageID;

/* A single page in memory. */
typedef struct {
    PageID  page_id;
    char    data[BP_PAGE_SIZE];
    int     is_dirty;      /* 1 if modified since read */
    int     pin_count;     /* number of active users */
    int     clock_bit;     /* CLOCK eviction reference bit */
} Page;

/* Buffer pool frame: a slot in the pool. */
typedef struct {
    Page    page;
    int     in_use;        /* 1 if frame holds a valid page */
    int     frame_id;
} BPFrame;

/* Main buffer pool manager. */
typedef struct {
    BPFrame  frames[BP_MAX_FRAMES];
    int      num_frames;
    int      clock_hand;   /* position for CLOCK eviction */
    int      hit_count;
    int      miss_count;
    int      evict_count;
} BufferPool;

/* --- API --- */

/* Initialize buffer pool with given number of frames (max BP_MAX_FRAMES). */
void     bp_init(BufferPool *bp, int max_frames);

/*
 * Fetch a page from the buffer pool.
 * If page is cached: pin it, set clock_bit, return frame index.
 * If not cached: read from disk (simulated), evict if needed, cache it.
 * Returns frame index >= 0, or -1 on error.
 */
int      bp_fetch(BufferPool *bp, PageID page_id);

/* Pin a page (increment pin_count). */
void     bp_pin(BufferPool *bp, int frame_idx);

/* Unpin a page (decrement pin_count). */
void     bp_unpin(BufferPool *bp, int frame_idx);

/* Mark a page as dirty (will be written back). */
void     bp_mark_dirty(BufferPool *bp, int frame_idx);

/* Get pointer to page data. Returns NULL on invalid index. */
char    *bp_get_page_data(BufferPool *bp, int frame_idx);

/* Flush a specific page (write dirty pages to disk). Returns 0 on success. */
int      bp_flush_page(BufferPool *bp, int frame_idx);

/* Flush all dirty pages. */
void     bp_flush_all(BufferPool *bp);

/* Get statistics. */
void     bp_stats(const BufferPool *bp, int *hits, int *misses, int *evictions);

/* Destroy pool (flush all and clear). */
void     bp_destroy(BufferPool *bp);

/*
 * Simulated disk read/write for unit testing.
 * These are called by bp_fetch and bp_flush_page internally.
 */
int      bp_disk_read(PageID page_id, char *buf, size_t size);
int      bp_disk_write(PageID page_id, const char *buf, size_t size);

#endif
