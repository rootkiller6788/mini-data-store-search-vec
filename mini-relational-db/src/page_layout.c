#include "page_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Page Layout Manager - Slotted Page Implementation
 *
 * Reference: PostgreSQL src/include/storage/bufpage.h
 *            CMU 15-445 Lecture 3: Database Storage I
 *
 * The slotted page is the fundamental storage abstraction in most
 * relational databases. It stores variable-length tuples efficiently
 * using a slot directory at the page front and tuple data growing
 * backward from the page end.
 *
 * Layout:
 *   [PageHeader] [Slot 0] [Slot 1] ... [free space] ... [Tuple N] [Tuple N-1]
 *   low addr ----->                                       <---- high addr
 *
 * Properties:
 *   - Tuples can be reordered without moving data (swap slots)
 *   - Deletion just clears slot flag (fast, space reused at compaction)
 *   - Free space region is always contiguous between slots and tuples
 */

void page_init(SlottedPage *page) {
    if (!page) return;
    memset(page, 0, sizeof(SlottedPage));
    page->header.num_slots = 0;
    page->header.free_start = sizeof(PageHeader) + sizeof(SlotEntry) * SLOT_MAX_TUPLES;
    page->header.free_end   = SLOT_PAGE_SIZE;
    page->header.page_flags = 0;
    page->header.reserved   = 0;
}

/*
 * Insert a tuple into the slotted page.
 *
 * Algorithm:
 *   1. Find an unused slot (flags == 0) or use the next available slot.
 *   2. Check if enough free space: need sizeof(SlotEntry) + data_len.
 *   3. Move free_end backward by data_len, copy tuple data.
 *   4. Set slot offset and length, mark flags = 1 (normal).
 *
 * Constant overhead per tuple = sizeof(SlotEntry) = 6 bytes.
 *
 * Returns slot number (>= 0) or -1 if space exhausted.
 */
int page_insert_tuple(SlottedPage *page, const char *data, uint16_t len) {
    if (!page || !data || len == 0) return -1;
    if (page->header.num_slots >= SLOT_MAX_TUPLES) return -1;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < SLOT_MAX_TUPLES; i++) {
        if (page->slots[i].flags == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    /* Check space */
    uint16_t needed = len;
    uint16_t free_bytes = page->header.free_end - page->header.free_start;
    if (needed > free_bytes) return -1;

    /* Allocate space at end of free region */
    page->header.free_end -= needed;
    memcpy(page->data + (page->header.free_end - (sizeof(PageHeader) + sizeof(SlotEntry) * SLOT_MAX_TUPLES)),
           data, len);

    /* Set slot entry: offset measured from page start */
    page->slots[slot].offset = page->header.free_end;
    page->slots[slot].length = len;
    page->slots[slot].flags  = 1;  /* normal, in-use */

    if (slot >= page->header.num_slots)
        page->header.num_slots = slot + 1;

    return slot;
}

/*
 * Read tuple at slot into caller-provided buffer.
 */
int page_get_tuple(const SlottedPage *page, int slot, char *buf, uint16_t *len) {
    if (!page || slot < 0 || slot >= page->header.num_slots) return -1;
    if (page->slots[slot].flags != 1) return -1;

    const SlotEntry *se = &page->slots[slot];
    uint16_t data_offset = se->offset - (sizeof(PageHeader) + sizeof(SlotEntry) * SLOT_MAX_TUPLES);

    if (buf)
        memcpy(buf, page->data + data_offset, se->length);
    if (len)
        *len = se->length;
    return 0;
}

int page_delete_tuple(SlottedPage *page, int slot) {
    if (!page || slot < 0 || slot >= page->header.num_slots) return -1;
    if (page->slots[slot].flags != 1) return -1;

    page->slots[slot].flags = 0;
    return 0;
}

/*
 * Compaction (VACUUM-lite): move all live tuples to eliminate fragmentation.
 *
 * Algorithm:
 *   1. Scan all slots, collect live tuples.
 *   2. Place them consecutively from end of free region.
 *   3. Update slot offsets.
 *
 * Returns number of bytes reclaimed.
 */
int page_compact(SlottedPage *page) {
    if (!page) return 0;

    /* Collect live tuples and their lengths */
    uint16_t tuple_data[SLOT_MAX_TUPLES];
    int      live_count = 0;
    int      live_slots[SLOT_MAX_TUPLES];

    for (int i = 0; i < page->header.num_slots; i++) {
        if (page->slots[i].flags == 1) {
            live_slots[live_count] = i;
            tuple_data[live_count] = page->slots[i].length;
            live_count++;
        }
    }

    if (live_count == 0) {
        /* All deleted - reset page */
        page_init(page);
        return SLOT_PAGE_SIZE - sizeof(PageHeader) - (int)sizeof(SlotEntry) * SLOT_MAX_TUPLES;
    }

    /* Calculate new positions */
    uint16_t new_end = SLOT_PAGE_SIZE;
    for (int i = live_count - 1; i >= 0; i--) {
        uint16_t len = tuple_data[i];
        new_end -= len;
        uint16_t data_offset = new_end - (sizeof(PageHeader) + sizeof(SlotEntry) * SLOT_MAX_TUPLES);
        uint16_t old_offset = page->slots[live_slots[i]].offset
                            - (sizeof(PageHeader) + sizeof(SlotEntry) * SLOT_MAX_TUPLES);
        if (old_offset != data_offset) {
            memmove(page->data + data_offset, page->data + old_offset, len);
        }
        page->slots[live_slots[i]].offset = new_end;
    }

    uint16_t old_free = page->header.free_end;
    page->header.free_end = new_end;

    return (int)(old_free - new_end);
}

int page_free_space(const SlottedPage *page) {
    if (!page) return 0;
    return page->header.free_end - page->header.free_start;
}

int page_slot_is_used(const SlottedPage *page, int slot) {
    if (!page || slot < 0 || slot >= page->header.num_slots) return 0;
    return page->slots[slot].flags == 1;
}

void page_dump(const SlottedPage *page) {
    if (!page) { printf("SlottedPage: (null)\n"); return; }
    printf("SlottedPage: %d slots, free=%d bytes (start=%u end=%u)\n",
           page->header.num_slots,
           page->header.free_end - page->header.free_start,
           page->header.free_start, page->header.free_end);
    for (int i = 0; i < page->header.num_slots; i++) {
        const SlotEntry *se = &page->slots[i];
        printf("  [%d] offset=%u len=%u flags=%u",
               i, se->offset, se->length, se->flags);
        if (se->flags == 1) {
            uint16_t off = se->offset - (sizeof(PageHeader) + sizeof(SlotEntry) * SLOT_MAX_TUPLES);
            printf(" data=%.*s", se->length < 40 ? (int)se->length : 40,
                   page->data + off);
        }
        printf("\n");
    }
}
