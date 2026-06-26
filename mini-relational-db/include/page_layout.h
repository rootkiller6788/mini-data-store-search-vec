#ifndef PAGE_LAYOUT_H
#define PAGE_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Page Layout Manager — L3 Engineering Structure: Slotted Page.
 *
 * Reference: PostgreSQL Buffer Manager (src/include/storage/bufpage.h)
 *
 * A slotted page stores variable-length tuples with header at the front
 * and tuple data growing from the end. Slot array tracks offsets.
 *
 * Layout (within BP_PAGE_SIZE byte page):
 *   [PageHeader][Slot0][Slot1]...[free space]...[TupleN]...[Tuple1][Tuple0]
 *
 * Each slot stores: (offset, length, flags)
 * Free space is managed between slot array growth and tuple growth.
 */

#define SLOT_MAX_TUPLES 64
#define SLOT_PAGE_SIZE  4096

/* Line pointer (slot entry): points to a tuple in the page. */
typedef struct {
    uint16_t offset;       /* byte offset from page start */
    uint16_t length;       /* tuple length in bytes */
    uint8_t  flags;        /* 0=unused, 1=normal, 2=redirect */
} SlotEntry;

/* Page header. */
typedef struct {
    uint16_t  num_slots;          /* number of slots used */
    uint16_t  free_start;         /* start of free space */
    uint16_t  free_end;           /* end of free space (grows downward) */
    uint8_t   page_flags;         /* 0=normal */
    uint16_t  reserved;
} PageHeader;

/* A slotted page (fits in BP_PAGE_SIZE). */
typedef struct {
    PageHeader header;
    SlotEntry  slots[SLOT_MAX_TUPLES];
    char       data[SLOT_PAGE_SIZE - sizeof(PageHeader) - sizeof(SlotEntry) * SLOT_MAX_TUPLES];
} SlottedPage;

/* --- API --- */

/* Initialize an empty slotted page. */
void     page_init(SlottedPage *page);

/*
 * Insert a tuple into the page at the next available slot.
 * Returns the slot number (>=0) or -1 if no space.
 * Tuple data is copied into the page body.
 */
int      page_insert_tuple(SlottedPage *page, const char *data, uint16_t len);

/*
 * Read tuple at slot. Places data in buf, sets *len to tuple length.
 * Returns 0 on success, -1 if slot is invalid/unused.
 */
int      page_get_tuple(const SlottedPage *page, int slot, char *buf, uint16_t *len);

/*
 * Delete tuple at slot (marks slot as unused, does not reclaim space).
 * Actual space reclamation happens at VACUUM/compaction time.
 * Returns 0 on success, -1 if slot is invalid.
 */
int      page_delete_tuple(SlottedPage *page, int slot);

/*
 * Compact page: defragment by moving all live tuples to remove gaps.
 * Returns bytes reclaimed.
 */
int      page_compact(SlottedPage *page);

/* Get free space remaining in the page. */
int      page_free_space(const SlottedPage *page);

/* Check if a slot is in use. */
int      page_slot_is_used(const SlottedPage *page, int slot);

/* Debug: dump page layout. */
void     page_dump(const SlottedPage *page);

#endif
