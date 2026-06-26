#include "buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Buffer Pool Manager Implementation - L3 Engineering Structure
 *
 * Reference: CMU 15-445 Lecture 4: Buffer Pools
 *            PostgreSQL buf/bufmgr.c
 *
 * Implements the CLOCK eviction policy (approximation of LRU).
 *
 * CLOCK Algorithm (Corbato, 1968):
 *   Maintain a circular buffer of frames with a reference bit (clock_bit).
 *   On eviction:
 *     1. Advance clock_hand.
 *     2. If clock_bit == 1: clear it and continue (give second chance).
 *     3. If clock_bit == 0: evict this page (after write-back if dirty).
 *   This provides O(1) amortized eviction vs true LRU O(n).
 *
 * L4 Theorem (Belady's Optimal): CLOCK is O(1) online approximation of LRU.
 *   Competitive ratio is O(k) where k = cache size.
 */

/* Simulated disk storage for unit testing. */
#define DISK_MAX_PAGES 64
static struct {
    char data[BP_PAGE_SIZE];
    int  initialized;
} disk_pages[DISK_MAX_PAGES];

/* Hash page ID to disk slot. */
static int disk_hash(PageID pid) {
    return (pid.file_id * 31 + pid.page_number) % DISK_MAX_PAGES;
}

void bp_init(BufferPool *bp, int max_frames) {
    if (!bp) return;
    memset(bp, 0, sizeof(BufferPool));
    bp->num_frames = (max_frames > 0 && max_frames <= BP_MAX_FRAMES)
                     ? max_frames : BP_MAX_FRAMES;
    bp->clock_hand = 0;
    bp->hit_count = 0;
    bp->miss_count = 0;
    bp->evict_count = 0;
    /* Note: disk_pages is NOT cleared here to allow persistence across
     * bp_destroy/bp_init cycles (important for flush/readback tests). */
}

int bp_disk_read(PageID page_id, char *buf, size_t size) {
    int slot = disk_hash(page_id);
    if (!disk_pages[slot].initialized) {
        memset(buf, 0, size);
        return -1;
    }
    memcpy(buf, disk_pages[slot].data, size < BP_PAGE_SIZE ? size : BP_PAGE_SIZE);
    return 0;
}

int bp_disk_write(PageID page_id, const char *buf, size_t size) {
    int slot = disk_hash(page_id);
    memcpy(disk_pages[slot].data, buf, size < BP_PAGE_SIZE ? size : BP_PAGE_SIZE);
    disk_pages[slot].initialized = 1;
    return 0;
}

/*
 * Find a frame to evict using CLOCK algorithm.
 * Only considers unpinned frames (pin_count == 0).
 * Returns frame index or -1 if all frames are pinned.
 */
static int clock_evict(BufferPool *bp) {
    (void)bp->clock_hand;
    int attempts = 0;

    while (attempts < 2 * bp->num_frames) {
        BPFrame *frame = &bp->frames[bp->clock_hand];
        if (!frame->in_use || frame->page.pin_count > 0) {
            bp->clock_hand = (bp->clock_hand + 1) % bp->num_frames;
            attempts++;
            continue;
        }

        if (frame->page.clock_bit == 1) {
            frame->page.clock_bit = 0;
            bp->clock_hand = (bp->clock_hand + 1) % bp->num_frames;
            attempts++;
            continue;
        }

        /* Evict this frame */
        if (frame->page.is_dirty) {
            bp_disk_write(frame->page.page_id, frame->page.data, BP_PAGE_SIZE);
        }

        int evicted = bp->clock_hand;
        bp->clock_hand = (bp->clock_hand + 1) % bp->num_frames;
        bp->evict_count++;
        return evicted;
    }

    return -1; /* all frames pinned */
}

/*
 * Find an empty frame (not in_use).
 * If none, evict one using CLOCK.
 * Returns frame index or -1.
 */
static int bp_find_frame(BufferPool *bp) {
    for (int i = 0; i < bp->num_frames; i++) {
        if (!bp->frames[i].in_use)
            return i;
    }
    return clock_evict(bp);
}

int bp_fetch(BufferPool *bp, PageID page_id) {
    /* Check if already in pool */
    for (int i = 0; i < bp->num_frames; i++) {
        BPFrame *f = &bp->frames[i];
        if (f->in_use &&
            f->page.page_id.file_id == page_id.file_id &&
            f->page.page_id.page_number == page_id.page_number) {
            bp->hit_count++;
            f->page.clock_bit = 1;
            f->page.pin_count++;
            return i;
        }
    }

    /* Cache miss - find a frame */
    bp->miss_count++;
    int idx = bp_find_frame(bp);
    if (idx < 0) return -1;

    BPFrame *f = &bp->frames[idx];
    f->in_use = 1;
    f->frame_id = idx;
    f->page.page_id = page_id;
    f->page.is_dirty = 0;
    f->page.pin_count = 1;
    f->page.clock_bit = 1;

    bp_disk_read(page_id, f->page.data, BP_PAGE_SIZE);
    return idx;
}

void bp_pin(BufferPool *bp, int frame_idx) {
    if (!bp || frame_idx < 0 || frame_idx >= bp->num_frames) return;
    if (bp->frames[frame_idx].in_use)
        bp->frames[frame_idx].page.pin_count++;
}

void bp_unpin(BufferPool *bp, int frame_idx) {
    if (!bp || frame_idx < 0 || frame_idx >= bp->num_frames) return;
    if (bp->frames[frame_idx].in_use && bp->frames[frame_idx].page.pin_count > 0)
        bp->frames[frame_idx].page.pin_count--;
}

void bp_mark_dirty(BufferPool *bp, int frame_idx) {
    if (!bp || frame_idx < 0 || frame_idx >= bp->num_frames) return;
    if (bp->frames[frame_idx].in_use)
        bp->frames[frame_idx].page.is_dirty = 1;
}

char *bp_get_page_data(BufferPool *bp, int frame_idx) {
    if (!bp || frame_idx < 0 || frame_idx >= bp->num_frames) return NULL;
    if (!bp->frames[frame_idx].in_use) return NULL;
    return bp->frames[frame_idx].page.data;
}

int bp_flush_page(BufferPool *bp, int frame_idx) {
    if (!bp || frame_idx < 0 || frame_idx >= bp->num_frames) return -1;
    BPFrame *f = &bp->frames[frame_idx];
    if (!f->in_use) return -1;
    if (f->page.is_dirty) {
        bp_disk_write(f->page.page_id, f->page.data, BP_PAGE_SIZE);
        f->page.is_dirty = 0;
    }
    return 0;
}

void bp_flush_all(BufferPool *bp) {
    if (!bp) return;
    for (int i = 0; i < bp->num_frames; i++) {
        bp_flush_page(bp, i);
    }
}

void bp_stats(const BufferPool *bp, int *hits, int *misses, int *evictions) {
    if (!bp) return;
    if (hits)      *hits = bp->hit_count;
    if (misses)    *misses = bp->miss_count;
    if (evictions) *evictions = bp->evict_count;
}

void bp_destroy(BufferPool *bp) {
    if (!bp) return;
    bp_flush_all(bp);
    memset(bp, 0, sizeof(BufferPool));
}
