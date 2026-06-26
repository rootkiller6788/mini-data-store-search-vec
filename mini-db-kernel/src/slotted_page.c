#include "slotted_page.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L3: Slotted Page 实现
 *
 * Slotted Page 是数据库页面组织的核心工程结构。每页有一个固定大小的
 * Header，后面跟着一个 Slot Array（从低地址向高地址增长），页面数据
 * (Tuple Data) 从高地址向低地址增长。两者之间的空隙即为 Free Space。
 *
 * 页面布局:
 *  Offset 0      ┌──────────────┐
 *                │ PageHeader   │ (64 bytes)
 *  Offset 64     ├──────────────┤
 *                │ SlotArray[0] │ (offset, length)
 *                │ SlotArray[1] │
 *                │     ...      │ ← free_start (pointer moves right)
 *                │   FREE SPACE │
 *                │     ...      │ ← free_end (pointer moves left)
 *                │  Tuple N     │
 *                │  Tuple N-1   │
 *  Offset 4095   └──────────────┘
 *
 * 对应教材: Database Internals (Petrov) Chapter 3: File Organization
 * 课程映射: CMU 15-445 Lecture 04-05, Project 1
 */

void sp_init_page(SlottedPage *page, int32_t page_id) {
    memset(page, 0, sizeof(*page));
    page->header.page_id = page_id;
    page->header.lsn = 0;
    page->header.slot_count = 0;
    page->header.tuple_count = 0;
    page->header.free_start = SP_HEADER_SIZE;
    page->header.free_end = SP_PAGE_SIZE;
    page->header.dirty = false;
}

/*
 * 插入 Tuple: 在页面末尾分配空间，Slot Array 中添加新条目
 * 复杂度: O(1) 均摊, 需要 compact 时为 O(n)
 */
bool sp_insert_tuple(SlottedPage *page, const uint8_t *tuple_data, 
                     int32_t tuple_size, int32_t *slot_out) {
    if (!page || !tuple_data || tuple_size <= 0) return false;
    if (page->header.slot_count >= SP_MAX_SLOTS) return false;

    int32_t required = tuple_size;
    int32_t free_space = page->header.free_end - page->header.free_start;

    if (required > free_space) {
        if (!sp_compact(page)) return false;
        free_space = page->header.free_end - page->header.free_start;
        if (required > free_space) return false;
    }

    int32_t slot_idx = page->header.slot_count;
    page->header.free_end -= required;
    page->slots[slot_idx].offset = page->header.free_end;
    page->slots[slot_idx].length = required;
    memcpy(page->data + page->header.free_end, tuple_data, required);

    page->header.slot_count++;
    page->header.tuple_count++;
    page->header.free_start = SP_HEADER_SIZE +
        (int32_t)(page->header.slot_count * sizeof(SlotEntry));
    page->header.dirty = true;

    if (slot_out) *slot_out = slot_idx;
    return true;
}

/*
 * 删除 Tuple: 将 slot length 设为 0 (逻辑删除)
 * 空间不立即回收，需要 compact 才真正释放
 * 对应 PostgreSQL 中的 HeapTuple 删除: 先标记 xmax, 再 vacuum
 */
bool sp_delete_tuple(SlottedPage *page, int32_t slot_id) {
    if (!page || slot_id < 0 || slot_id >= page->header.slot_count) return false;
    if (page->slots[slot_id].length == 0) return false;

    page->slots[slot_id].length = 0;
    page->header.tuple_count--;
    page->header.dirty = true;
    return true;
}

/*
 * 更新 Tuple:
 * L2: In-Place Update vs Out-of-Place Update
 * 如果新数据 <= 原数据大小，原地更新 (in-place)
 * 否则标记旧 slot 为删除，插入新 tuple (out-of-place -> MVCC style)
 *
 * 工程含义: Postgres 使用 out-of-place (heap_update -> new tuple + old xmax)
 *           MySQL InnoDB 对于非变长列也使用 in-place update
 */
bool sp_update_tuple(SlottedPage *page, int32_t slot_id,
                     const uint8_t *new_data, int32_t new_size) {
    if (!page || !new_data || new_size <= 0) return false;
    if (slot_id < 0 || slot_id >= page->header.slot_count) return false;
    if (page->slots[slot_id].length == 0) return false;

    int32_t old_size = page->slots[slot_id].length;

    if (new_size <= old_size) {
        /* In-place update */
        memcpy(page->data + page->slots[slot_id].offset, new_data, new_size);
        page->slots[slot_id].length = new_size;
    } else {
        /* Out-of-place update: delete old entry, insert new tuple */
        page->slots[slot_id].length = 0;
        page->header.tuple_count--;
        int32_t new_slot;
        if (!sp_insert_tuple(page, new_data, new_size, &new_slot)) return false;
    }

    page->header.dirty = true;
    return true;
}

/*
 * 读取 Tuple: 通过 slot_id 获取 tuple 数据
 * 调用者负责提供足够大的 data_out 缓冲区
 */
bool sp_get_tuple(SlottedPage *page, int32_t slot_id,
                  uint8_t *data_out, int32_t *size_out) {
    if (!page || !data_out || !size_out) return false;
    if (slot_id < 0 || slot_id >= page->header.slot_count) return false;
    if (page->slots[slot_id].length == 0) return false;

    int32_t len = page->slots[slot_id].length;
    int32_t off = page->slots[slot_id].offset;
    memcpy(data_out, page->data + off, len);
    *size_out = len;
    return true;
}

/* 计算页面中的空闲空间 */
int32_t sp_get_free_space(const SlottedPage *page) {
    if (!page) return 0;
    return page->header.free_end - page->header.free_start;
}

/* 计算已使用的空间 (包括 header, slots, 和 tuple data) */
int32_t sp_get_used_space(const SlottedPage *page) {
    if (!page) return 0;
    int32_t total = SP_PAGE_SIZE - (page->header.free_end - page->header.free_start);
    return total;
}

/*
 * 页面压缩 (Compact / Defragment):
 * L5: 将所有有效 tuple 向页面末尾移动，消除碎片
 *
 * 对应 PostgreSQL 的 VACUUM FULL 或 MySQL InnoDB 的页面重组
 * 算法:
 *   1. 从后往前收集所有有效 tuple 的 data
 *   2. 紧凑排列在页面末尾
 *   3. 更新 slot offset 指向新位置
 *   4. 重设 free_start, free_end
 *
 * 复杂度: O(n * m) where n = active tuples, m = max tuple size
 */
bool sp_compact(SlottedPage *page) {
    if (!page) return false;

    int32_t active_count = 0;
    int32_t total_data = 0;

    /* Pass 1: count active tuples and total needed space */
    for (int32_t i = 0; i < page->header.slot_count; i++) {
        if (page->slots[i].length > 0) {
            active_count++;
            total_data += page->slots[i].length;
        }
    }
    if (active_count == page->header.slot_count) return true;

    /* Pass 2: compact tuple data into a temporary buffer */
    uint8_t *temp = (uint8_t *)malloc((size_t)total_data);
    if (!temp) return false;

    int32_t *new_offsets = (int32_t *)malloc(sizeof(int32_t) * (size_t)active_count);
    if (!new_offsets) { free(temp); return false; }

    int32_t *slot_map = (int32_t *)malloc(sizeof(int32_t) * (size_t)active_count);
    if (!slot_map) { free(temp); free(new_offsets); return false; }

    int32_t cur = 0, idx = 0;
    for (int32_t i = 0; i < page->header.slot_count; i++) {
        if (page->slots[i].length > 0) {
            memcpy(temp + cur, page->data + page->slots[i].offset, 
                   (size_t)page->slots[i].length);
            new_offsets[idx] = cur;
            slot_map[idx] = i;
            cur += page->slots[i].length;
            idx++;
        }
    }

    /* Pass 3: write compacted data back, update slots */
    page->header.free_end = SP_PAGE_SIZE - total_data;
    memcpy(page->data + page->header.free_end, temp, (size_t)total_data);

    for (int32_t j = 0; j < idx; j++) {
        int32_t si = slot_map[j];
        page->slots[si].offset = page->header.free_end + new_offsets[j];
    }

    page->header.free_start = SP_HEADER_SIZE + 
        (int32_t)(page->header.slot_count * sizeof(SlotEntry));
    page->header.dirty = true;

    free(slot_map);
    free(new_offsets);
    free(temp);
    return true;
}

/*
 * 遍历页面中的所有有效 tuple
 * visitor 返回 false 时停止遍历
 * 返回值为访问的 tuple 数量
 *
 * L5: Iterator Pattern - 遍历抽象
 */
int32_t sp_iterate_tuples(SlottedPage *page,
                          bool (*visitor)(int32_t slot_id, const uint8_t *data, 
                                         int32_t size, void *ctx),
                          void *ctx) {
    if (!page || !visitor) return 0;
    int32_t visited = 0;
    for (int32_t i = 0; i < page->header.slot_count; i++) {
        if (page->slots[i].length > 0) {
            if (!visitor(i, page->data + page->slots[i].offset, 
                        page->slots[i].length, ctx))
                break;
            visited++;
        }
    }
    return visited;
}

bool sp_copy_page(const SlottedPage *src, SlottedPage *dst) {
    if (!src || !dst) return false;
    memcpy(dst, src, sizeof(*src));
    return true;
}

void sp_print_page(const SlottedPage *page) {
    if (!page) { printf("NULL page\n"); return; }
    printf("Page %d: %d slots, %d tuples, free=%d, lsn=%lld\n",
           page->header.page_id, page->header.slot_count, page->header.tuple_count,
           sp_get_free_space(page), (long long)page->header.lsn);
    for (int32_t i = 0; i < page->header.slot_count; i++) {
        if (page->slots[i].length > 0) {
            printf("  slot[%d]: offset=%d len=%d data=", 
                   i, page->slots[i].offset, page->slots[i].length);
            int32_t max_print = page->slots[i].length < 32 ? page->slots[i].length : 32;
            for (int32_t j = 0; j < max_print; j++) {
                printf("%02x ", page->data[page->slots[i].offset + j]);
            }
            printf(page->slots[i].length > 32 ? "..." : "");
            printf("\n");
        } else {
            printf("  slot[%d]: (deleted)\n", i);
        }
    }
}