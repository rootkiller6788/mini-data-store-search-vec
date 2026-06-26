#include "kv_store.h"
#include "btree.h"
#include "mvcc.h"
#include "slotted_page.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L7: KV Store — Key-Value 存储引擎应用
 *
 * 本模块整合 mini-db-kernel 的所有子模块，实现一个完整的、
 * 可运行的 KV 存储引擎。展示数据库内核各组件如何在实际应用
 * 中协同工作。
 *
 * 架构:
 *   ┌────────────────────────────────────┐
 *   │         KV Store API               │
 *   │  (put, get, delete, scan, txn)     │
 *   ├────────────────────────────────────┤
 *   │  B+Tree (Index)  │  MVCC (Isolation)│
 *   ├────────────────────────────────────┤
 *   │  Buffer Pool     │  Lock Manager   │
 *   ├────────────────────────────────────┤
 *   │  Slotted Page    │  Disk Manager   │
 *   ├────────────────────────────────────┤
 *   │  WAL (Durability)                  │
 *   └────────────────────────────────────┘
 *
 * 数据流:
 *   Write: kvs_put → B+Tree insert → Buffer Pool → WAL write → Disk
 *   Read:  kvs_get → B+Tree search → Buffer Pool → MVCC check → return
 *
 * 对应 CMU 15-445 Project 1-4 的综合应用
 */

/* === Helper: hash a key buffer to an int32_t ===
 * L4: 哈希函数 — DJB2 算法 (Daniel J. Bernstein)
 *
 * DJB2 是一个简单但分布良好的字符串哈希函数。
 * 用于将可变长度的 key 映射为固定长度的 int32_t，
 * 以便 B+Tree 和 Lock Manager 使用整数 key。
 *
 * 属性: 均匀分布 (uniform distribution)，低碰撞率
 * 复杂度: O(n) where n = key_len
 */
static int32_t hash_key(const uint8_t *key, int32_t key_len) {
    uint32_t hash = 5381;
    for (int32_t i = 0; i < key_len; i++) {
        hash = ((hash << 5) + hash) + (uint32_t)key[i]; /* hash * 33 + c */
    }
    return (int32_t)(hash & 0x7FFFFFFF);
}

/* Convenience macros for accessing sub-modules */
#define KVS_BP(store)  ((BufferPool *)((store)->bp_handle))
#define KVS_WAL(store) ((WALManager *)((store)->wal_handle))
#define KVS_LM(store)  ((LockManager *)((store)->lm_handle))
#define KVS_DM(store)  ((DiskManager *)((store)->dm_handle))

/* === Lifecycle === */
void kvs_init(KVStore *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));

    /* 初始化各子模块 */
    BufferPool *bp = (BufferPool *)malloc(sizeof(BufferPool));
    if (bp) bp_init(bp);
    store->bp_handle = bp;

    WALManager *wm = (WALManager *)malloc(sizeof(WALManager));
    if (wm) wal_init(wm);
    store->wal_handle = wm;

    LockManager *lm = (LockManager *)malloc(sizeof(LockManager));
    if (lm) lm_init(lm);
    store->lm_handle = lm;

    DiskManager *dm = (DiskManager *)malloc(sizeof(DiskManager));
    if (dm) dm_init(dm, "kvstore.db");
    store->dm_handle = dm;

    /* B+Tree: 通过 btree_create() 创建根节点 */
    store->btree = (void *)btree_create();

    mvcc_init();

    store->next_txn_id = 1;
    store->num_txns = 0;
    memset(store->txns, 0, sizeof(store->txns));
    memset(&store->stats, 0, sizeof(store->stats));
    store->initialized = true;

    printf("[KVS] KV Store initialized\n");
}

void kvs_destroy(KVStore *store) {
    if (!store) return;
    WALManager *wm = KVS_WAL(store);
    LockManager *lm = KVS_LM(store);
    BufferPool *bp = KVS_BP(store);
    DiskManager *dm = KVS_DM(store);

    if (wm) { free(wm->records); free(wm); }
    if (lm)  { free(lm); }
    if (bp)  { free(bp); }
    if (dm)  { dm_destroy(dm); free(dm); }
    store->wal_handle = NULL;
    store->lm_handle = NULL;
    store->bp_handle = NULL;
    store->dm_handle = NULL;
    memset(store, 0, sizeof(*store));
    printf("[KVS] KV Store destroyed\n");
}

/* === Transaction Management ===
 * L2: ACID 事务管理
 */

int32_t kvs_begin_txn(KVStore *store) {
    if (!store || !store->initialized) return -1;
    int32_t txn_id = mvcc_begin();
    if (store->num_txns < KVS_MAX_TXNS) {
        store->txns[store->num_txns].txn_id = txn_id;
        store->txns[store->num_txns].active = true;
        store->num_txns++;
    }
    return txn_id;
}

bool kvs_commit_txn(KVStore *store, int32_t txn_id) {
    if (!store || txn_id < 0) return false;
    WALManager *wm = KVS_WAL(store);
    LockManager *lm = KVS_LM(store);

    wal_write(wm, WAL_COMMIT, -1, 0, txn_id, NULL, 0, NULL, 0);
    wal_flush(wm, wm->next_lsn - 1);
    mvcc_commit(txn_id);
    lm_release_all(lm, txn_id);

    for (int32_t i = 0; i < store->num_txns; i++) {
        if (store->txns[i].txn_id == txn_id) {
            store->txns[i].active = false;
            break;
        }
    }
    store->stats.commits++;
    return true;
}

bool kvs_rollback_txn(KVStore *store, int32_t txn_id) {
    if (!store || txn_id < 0) return false;
    WALManager *wm = KVS_WAL(store);
    LockManager *lm = KVS_LM(store);

    wal_write(wm, WAL_ABORT, -1, 0, txn_id, NULL, 0, NULL, 0);
    mvcc_abort(txn_id);
    lm_release_all(lm, txn_id);

    for (int32_t i = 0; i < store->num_txns; i++) {
        if (store->txns[i].txn_id == txn_id) {
            store->txns[i].active = false;
            break;
        }
    }
    store->stats.aborts++;
    return true;
}

/* === CRUD Operations === */

bool kvs_put(KVStore *store, int32_t txn_id,
             const uint8_t *key, int32_t key_len,
             const uint8_t *value, int32_t value_len) {
    if (!store || !key || !value || key_len <= 0 || value_len <= 0) return false;
    if (key_len > KVS_MAX_KEY_SIZE || value_len > KVS_MAX_VALUE_SIZE) return false;

    int32_t int_key = hash_key(key, key_len);
    LockManager *lm = KVS_LM(store);
    WALManager *wm = KVS_WAL(store);

    if (!lm_lock_acquire(lm, txn_id, int_key, LOCK_EXCLUSIVE)) {
        if (!lm_lock_upgrade(lm, txn_id, int_key)) return false;
    }

    int32_t existing_val = 0;
    bool exists = (btree_search((BTree)store->btree, int_key, &existing_val));

    int32_t int_value = 0;
    memcpy(&int_value, value,
           value_len < (int32_t)sizeof(int_value) ? (size_t)value_len : sizeof(int_value));

    if (exists) {
        uint8_t old_data[8];
        int32_t old_val = existing_val;
        memcpy(old_data, &old_val, sizeof(old_val));
        wal_write(wm, WAL_UPDATE, int_key, 0, txn_id,
                  old_data, (int32_t)sizeof(old_val),
                  (const uint8_t *)&int_value, (int32_t)sizeof(int_value));
        /* Remove old entry to avoid duplicate keys in B+Tree */
        btree_delete((BTree *)&store->btree, int_key);
    } else {
        wal_write(wm, WAL_INSERT, int_key, 0, txn_id,
                  NULL, 0,
                  (const uint8_t *)&int_value, (int32_t)sizeof(int_value));
    }

    btree_insert((BTree *)&store->btree, int_key, int_value);
    store->stats.puts++;
    return true;
}

bool kvs_get(KVStore *store, int32_t txn_id,
             const uint8_t *key, int32_t key_len,
             uint8_t *value_out, int32_t *value_len_out) {
    if (!store || !key || !value_out || !value_len_out || key_len <= 0) return false;

    int32_t int_key = hash_key(key, key_len);
    LockManager *lm = KVS_LM(store);

    if (!lm_lock_acquire(lm, txn_id, int_key, LOCK_SHARED)) return false;

    int32_t int_value = 0;
    if (btree_search((BTree)store->btree, int_key, &int_value)) {
        *value_len_out = (int32_t)sizeof(int_value);
        memcpy(value_out, &int_value, (size_t)*value_len_out);
        store->stats.gets++;
        return true;
    }

    *value_len_out = 0;
    store->stats.gets++;
    return false;
}

bool kvs_delete(KVStore *store, int32_t txn_id,
                const uint8_t *key, int32_t key_len) {
    if (!store || !key || key_len <= 0) return false;

    int32_t int_key = hash_key(key, key_len);
    LockManager *lm = KVS_LM(store);
    WALManager *wm = KVS_WAL(store);

    if (!lm_lock_acquire(lm, txn_id, int_key, LOCK_EXCLUSIVE)) {
        if (!lm_lock_upgrade(lm, txn_id, int_key)) return false;
    }

    int32_t old_value = 0;
    bool exists = btree_search((BTree)store->btree, int_key, &old_value);

    if (exists) {
        uint8_t old_buf[8];
        memcpy(old_buf, &old_value, sizeof(old_value));
        wal_write(wm, WAL_DELETE, int_key, 0, txn_id,
                  old_buf, (int32_t)sizeof(old_value), NULL, 0);
    }

    btree_delete((BTree *)&store->btree, int_key);
    store->stats.deletes++;
    return true;
}

/* === Cursor-based Range Scan ===
 * L7: 范围扫描 (Range Scan / Cursor)
 */

bool kvs_scan_open(KVStore *store, int32_t txn_id,
                   int32_t start_key, int32_t end_key, KVCursor *cursor) {
    if (!store || !cursor) return false;
    LockManager *lm = KVS_LM(store);
    lm_lock_acquire(lm, txn_id, start_key, LOCK_SHARED);

    cursor->txn_id = txn_id;
    cursor->current_key = start_key;
    cursor->end_key = end_key;
    cursor->position = 0;

    cursor->keys = (int32_t *)malloc(sizeof(int32_t) * 256);
    cursor->values = (int32_t *)malloc(sizeof(int32_t) * 256);

    if (!cursor->keys || !cursor->values) {
        free(cursor->keys);
        free(cursor->values);
        cursor->keys = NULL;
        cursor->values = NULL;
        return false;
    }

    cursor->count = btree_search_range((BTree)store->btree, start_key, end_key,
                                        cursor->keys, cursor->values, 256);
    cursor->position = 0;
    cursor->exhausted = (cursor->count == 0);
    store->stats.scans++;
    return true;
}

bool kvs_scan_next(KVCursor *cursor, KVEntry *entry_out) {
    if (!cursor || !entry_out || cursor->exhausted) return false;
    if (cursor->position >= cursor->count) {
        cursor->exhausted = true;
        return false;
    }

    int32_t key = cursor->keys[cursor->position];
    int32_t val = cursor->values[cursor->position];
    cursor->position++;

    memcpy(entry_out->key, &key, sizeof(key));
    entry_out->key_len = (int32_t)sizeof(key);
    memcpy(entry_out->value, &val, sizeof(val));
    entry_out->value_len = (int32_t)sizeof(val);
    return true;
}

void kvs_scan_close(KVCursor *cursor) {
    if (!cursor) return;
    free(cursor->keys);
    free(cursor->values);
    cursor->keys = NULL;
    cursor->values = NULL;
    cursor->count = 0;
}

/* === Utilities === */

void kvs_print_stats(const KVStore *store) {
    if (!store) return;
    printf("=== KV Store Statistics ===\n");
    printf("Gets: %lld, Puts: %lld, Deletes: %lld\n",
           (long long)store->stats.gets, (long long)store->stats.puts,
           (long long)store->stats.deletes);
    printf("Scans: %lld, Commits: %lld, Aborts: %lld\n",
           (long long)store->stats.scans, (long long)store->stats.commits,
           (long long)store->stats.aborts);
    printf("Active Transactions: %d\n", store->num_txns);
    if (store->bp_handle) bp_print_stats((BufferPool *)store->bp_handle);
    if (store->dm_handle) dm_print_stats((const DiskManager *)store->dm_handle);
}

bool kvs_recover(KVStore *store) {
    if (!store || !store->wal_handle) return false;
    printf("[KVS] Starting recovery...\n");
    wal_recover((WALManager *)store->wal_handle);
    printf("[KVS] Recovery complete\n");
    return true;
}