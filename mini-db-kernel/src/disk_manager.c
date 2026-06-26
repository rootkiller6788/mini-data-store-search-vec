#include "disk_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L3: Disk Manager — 模拟磁盘存储层
 *
 * Disk Manager 是数据库内核的最底层，负责:
 *   1. 页面分配 (allocate/deallocate)
 *   2. 页面读写 (read/write page to "disk")
 *   3. 数据库文件的创建和管理
 *
 * 本实现用内存数组模拟磁盘存储，所有"磁盘"页面存储在 dm->pages[] 中。
 * 真实系统中 Disk Manager 会调用 pread/pwrite 等系统调用。
 *
 * 对应 CMU 15-445 Project 0: C++ Primer / Disk Manager
 * 课程映射: CMU 15-445 Lecture 04 (Database Storage)
 *            Berkeley CS 186 (Database Systems)
 *
 * 关键设计决策:
 *   - 页面大小: 4KB (DM_PAGE_SIZE) — 与 Buffer Pool 的 BP_PAGE_SIZE 保持一致
 *   - 最大页面数: 65536 → 256MB 数据库大小上限
 *   - 模拟崩溃: dm_simulate_crash 用于测试恢复逻辑
 */

void dm_init(DiskManager *dm, const char *db_name) {
    memset(dm, 0, sizeof(*dm));
    strncpy(dm->db_name, db_name ? db_name : "default.db", DM_FILE_NAME_LEN - 1);
    dm->db_name[DM_FILE_NAME_LEN - 1] = '\0';
    dm->num_pages = 0;
    dm->read_count = 0;
    dm->write_count = 0;
    dm->simulated_crash = false;
    printf("[DM] Initialized database: %s\n", dm->db_name);
}

void dm_destroy(DiskManager *dm) {
    if (!dm) return;
    for (int32_t i = 0; i < DM_MAX_PAGES; i++) {
        if (dm->allocated[i]) {
            free(dm->pages[i]);
            dm->pages[i] = NULL;
            dm->allocated[i] = false;
        }
    }
    dm->num_pages = 0;
}

/*
 * 分配一个新页面
 * L4: Page Allocation Strategy — 简单的线性扫描分配器
 * 
 * 实现最简单的 first-fit 策略: 扫描 allocated[] 找到第一个空闲槽位。
 * 真实数据库使用:
 *   - Free Page List (freelist): PostgreSQL
 *   - Extent-based allocation: MySQL InnoDB (1MB extent = 256 pages)
 *   - Bitmap allocation: SQL Server
 *
 * 返回: 新的 page_id (≥ 0), 失败返回 -1
 */
int32_t dm_allocate_page(DiskManager *dm) {
    if (!dm) return -1;
    if (dm->simulated_crash) return -1;

    for (int32_t i = 0; i < DM_MAX_PAGES; i++) {
        if (!dm->allocated[i]) {
            dm->pages[i] = (uint8_t *)calloc(1, DM_PAGE_SIZE);
            if (!dm->pages[i]) return -1;
            dm->allocated[i] = true;
            dm->num_pages++;
            return i;
        }
    }
    return -1; /* 磁盘空间已满 */
}

void dm_deallocate_page(DiskManager *dm, int32_t page_id) {
    if (!dm || page_id < 0 || page_id >= DM_MAX_PAGES) return;
    if (!dm->allocated[page_id]) return;
    free(dm->pages[page_id]);
    dm->pages[page_id] = NULL;
    dm->allocated[page_id] = false;
    dm->num_pages--;
}

/*
 * 从"磁盘"读取页面到内存缓冲区
 * 崩溃模式下返回 false (模拟介质故障)
 */
bool dm_read_page(DiskManager *dm, int32_t page_id, uint8_t *buffer, int32_t buf_size) {
    if (!dm || !buffer || buf_size < DM_PAGE_SIZE) return false;
    if (page_id < 0 || page_id >= DM_MAX_PAGES) return false;
    if (dm->simulated_crash) return false;
    if (!dm->allocated[page_id]) return false;

    memcpy(buffer, dm->pages[page_id], DM_PAGE_SIZE);
    dm->read_count++;
    return true;
}

/*
 * 将内存缓冲区写入"磁盘"页面
 * 遵循 WAL 协议: 调用者必须在 write 之前先写 WAL 日志
 */
bool dm_write_page(DiskManager *dm, int32_t page_id, const uint8_t *buffer, int32_t buf_size) {
    if (!dm || !buffer || buf_size < DM_PAGE_SIZE) return false;
    if (page_id < 0 || page_id >= DM_MAX_PAGES) return false;
    if (dm->simulated_crash) return false;
    if (!dm->allocated[page_id]) return false;

    memcpy(dm->pages[page_id], buffer, DM_PAGE_SIZE);
    dm->write_count++;
    return true;
}

int32_t dm_get_num_pages(const DiskManager *dm) {
    return dm ? dm->num_pages : 0;
}

int64_t dm_get_read_count(const DiskManager *dm) {
    return dm ? dm->read_count : 0;
}

int64_t dm_get_write_count(const DiskManager *dm) {
    return dm ? dm->write_count : 0;
}

/*
 * 模拟崩溃: 用于测试恢复逻辑
 * crash=true 时所有 I/O 操作失败
 * crash=false 时恢复正常
 *
 * 对应 ARIES Recovery 中的系统崩溃场景测试
 */
void dm_simulate_crash(DiskManager *dm, bool crash) {
    if (!dm) return;
    dm->simulated_crash = crash;
    if (crash) {
        printf("[DM] *** SIMULATED CRASH *** All I/O blocked\n");
    } else {
        printf("[DM] Recovery from simulated crash\n");
    }
}

bool dm_is_crashed(const DiskManager *dm) {
    return dm ? dm->simulated_crash : false;
}

void dm_print_stats(const DiskManager *dm) {
    if (!dm) return;
    printf("Disk Manager: %s\n", dm->db_name);
    printf("  Pages: %d / %d\n", dm->num_pages, DM_MAX_PAGES);
    printf("  Reads: %lld, Writes: %lld\n",
           (long long)dm->read_count, (long long)dm->write_count);
    printf("  Total size: %.2f MB\n",
           (double)(dm->num_pages * DM_PAGE_SIZE) / (1024.0 * 1024.0));
    if (dm->simulated_crash) {
        printf("  Status: CRASHED (I/O blocked)\n");
    }
}