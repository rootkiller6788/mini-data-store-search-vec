#ifndef KV_STORE_H
#define KV_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define KVS_MAX_KEY_SIZE   64
#define KVS_MAX_VALUE_SIZE 256
#define KVS_MAX_TXNS       64
#define KVS_MAX_CURSORS    8

/*
 * L7: KV Store — 应用层 Key-Value 存储引擎
 * 
 * 集成 mini-db-kernel 全部子模块:
 *   - Buffer Pool: 页面缓存
 *   - B+Tree: 主索引 (key → value mapping)
 *   - WAL: 持久化日志
 *   - MVCC: 多版本并发控制
 *   - Lock Manager: 两阶段锁
 *   - Slotted Page: 页面内 tuple 组织
 *   - Disk Manager: 持久化存储抽象
 *
 * 本模块是对 CMU 15-445 Project 1-4 的完整串联，
 * 实现了 mini-db-kernel 的端到端数据流。
 */

typedef struct {
    uint8_t  key[KVS_MAX_KEY_SIZE];
    int32_t  key_len;
    uint8_t  value[KVS_MAX_VALUE_SIZE];
    int32_t  value_len;
} KVEntry;

typedef struct {
    int32_t txn_id;
    int32_t snapshot_xmin;
    bool    active;
} KVTransaction;

/* 游标: 用于范围扫描 */
typedef struct {
    int32_t  txn_id;
    int32_t  current_key;
    int32_t  end_key;
    int32_t *keys;
    int32_t *values;
    int32_t  count;
    int32_t  position;
    bool     exhausted;
} KVCursor;

#include "buffer_pool.h"
#include "wal.h"
#include "lock_manager.h"
#include "disk_manager.h"

/* 存储引擎 */
typedef struct {
    void             *bp_handle;
    void             *btree;
    void             *wal_handle;
    void             *lm_handle;
    void             *dm_handle;
    int32_t           next_txn_id;
    KVTransaction     txns[KVS_MAX_TXNS];
    int32_t           num_txns;
    struct {
        int64_t gets;
        int64_t puts;
        int64_t deletes;
        int64_t scans;
        int64_t commits;
        int64_t aborts;
    } stats;
    bool              initialized;
} KVStore;

/* ---- Lifecycle ---- */
void     kvs_init(KVStore *store);
void     kvs_destroy(KVStore *store);

/* ---- Transaction Management ---- */
int32_t  kvs_begin_txn(KVStore *store);
bool     kvs_commit_txn(KVStore *store, int32_t txn_id);
bool     kvs_rollback_txn(KVStore *store, int32_t txn_id);

/* ---- CRUD Operations ---- */
bool     kvs_put(KVStore *store, int32_t txn_id,
                 const uint8_t *key, int32_t key_len,
                 const uint8_t *value, int32_t value_len);
bool     kvs_get(KVStore *store, int32_t txn_id,
                 const uint8_t *key, int32_t key_len,
                 uint8_t *value_out, int32_t *value_len_out);
bool     kvs_delete(KVStore *store, int32_t txn_id,
                    const uint8_t *key, int32_t key_len);

/* ---- Cursor-based Range Scan ---- */
bool     kvs_scan_open(KVStore *store, int32_t txn_id,
                       int32_t start_key, int32_t end_key, KVCursor *cursor);
bool     kvs_scan_next(KVCursor *cursor, KVEntry *entry_out);
void     kvs_scan_close(KVCursor *cursor);

/* ---- Utilities ---- */
void     kvs_print_stats(const KVStore *store);
bool     kvs_recover(KVStore *store);

#endif
