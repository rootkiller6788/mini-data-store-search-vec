#ifndef SLOTTED_PAGE_H
#define SLOTTED_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SP_PAGE_SIZE        4096
#define SP_MAX_SLOTS        256
#define SP_HEADER_SIZE      64

/*
 * L1: Page Header — 数据库页面的元数据区
 * 对应 CMU 15-445 Lecture 04: Database Storage
 *
 * Slot Page (Slotted Page) 是数据库中最基础的页面组织方式。
 * 页面分为三个区域: Header → Slot Array → Tuple Data
 * Tuple 从页面末尾向前增长, Slot Array 从 Header 向后增长,
 * 中间的 Free Space 是两个区域之间的空隙。
 */

typedef struct {
    int32_t offset;
    int32_t length;
} SlotEntry;

typedef struct {
    int32_t  page_id;
    int64_t  lsn;
    int32_t  slot_count;
    int32_t  free_start;
    int32_t  free_end;
    int32_t  tuple_count;
    bool     dirty;
    uint8_t  reserved[19];
} PageHeader;

typedef struct {
    PageHeader header;
    SlotEntry  slots[SP_MAX_SLOTS];
    uint8_t    data[SP_PAGE_SIZE];
} SlottedPage;

void      sp_init_page(SlottedPage *page, int32_t page_id);
bool      sp_insert_tuple(SlottedPage *page, const uint8_t *tuple_data, int32_t tuple_size, int32_t *slot_out);
bool      sp_delete_tuple(SlottedPage *page, int32_t slot_id);
bool      sp_update_tuple(SlottedPage *page, int32_t slot_id, const uint8_t *new_data, int32_t new_size);
bool      sp_get_tuple(SlottedPage *page, int32_t slot_id, uint8_t *data_out, int32_t *size_out);
int32_t   sp_get_free_space(const SlottedPage *page);
int32_t   sp_get_used_space(const SlottedPage *page);
bool      sp_compact(SlottedPage *page);
int32_t   sp_iterate_tuples(SlottedPage *page,
                            bool (*visitor)(int32_t slot_id, const uint8_t *data, int32_t size, void *ctx),
                            void *ctx);
bool      sp_copy_page(const SlottedPage *src, SlottedPage *dst);
void      sp_print_page(const SlottedPage *page);

#endif
