#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define DM_MAX_PAGES  65536
#define DM_PAGE_SIZE  4096
#define DM_FILE_NAME_LEN 256

/*
 * L3: Disk Manager — 磁盘抽象层
 * 数据库内核与持久化存储之间的接口。
 * 本实现使用内存模拟磁盘 I/O，所有页面存储在内存数组中。
 *
 * 对应 CMU 15-445 Project 0 / Lecture 04: Disk Manager
 *
 * 关键概念:
 *   - Page allocation: 为表和索引分配新的磁盘页面
 *   - Read/Write: 以页面为单位的磁盘 I/O 操作
 *   - File organization: 堆文件、有序文件的页面管理
 */

typedef struct {
    uint8_t *pages[DM_MAX_PAGES];
    bool     allocated[DM_MAX_PAGES];
    int32_t  num_pages;
    int64_t  read_count;
    int64_t  write_count;
    bool     simulated_crash;
    char     db_name[DM_FILE_NAME_LEN];
} DiskManager;

typedef enum {
    DM_FILE_HEAP       = 0,
    DM_FILE_SORTED     = 1,
    DM_FILE_TREE       = 2,
    DM_FILE_HASH       = 3
} DMFileType;

typedef struct {
    int32_t    file_id;
    DMFileType type;
    int32_t    first_page_id;
    int32_t    last_page_id;
    int32_t    num_pages;
    int64_t    total_tuples;
    char       name[DM_FILE_NAME_LEN];
} DMFileInfo;

void     dm_init(DiskManager *dm, const char *db_name);
void     dm_destroy(DiskManager *dm);
int32_t  dm_allocate_page(DiskManager *dm);
void     dm_deallocate_page(DiskManager *dm, int32_t page_id);
bool     dm_read_page(DiskManager *dm, int32_t page_id, uint8_t *buffer, int32_t buf_size);
bool     dm_write_page(DiskManager *dm, int32_t page_id, const uint8_t *buffer, int32_t buf_size);
int32_t  dm_get_num_pages(const DiskManager *dm);
int64_t  dm_get_read_count(const DiskManager *dm);
int64_t  dm_get_write_count(const DiskManager *dm);
void     dm_simulate_crash(DiskManager *dm, bool crash);
bool     dm_is_crashed(const DiskManager *dm);
void     dm_print_stats(const DiskManager *dm);

#endif
