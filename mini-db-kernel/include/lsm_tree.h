#ifndef LSM_TREE_H
#define LSM_TREE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define LSM_MAX_LEVELS        8
#define LSM_MEMTABLE_MAX      256
#define LSM_SSTABLE_MAX_ENTRIES 256
#define LSM_BLOOM_BITS        1024
#define LSM_BLOOM_HASHES      4

/*
 * L8: LSM Tree (Log-Structured Merge Tree) — 写优化索引结构
 * 对应 CMU 15-445 Lecture 09: LSM Trees
 *
 * LSM Tree 是现代写优化存储引擎的基础 (LevelDB, RocksDB, Cassandra)
 * 与 B+Tree 形成互补: B+Tree 读优化, LSM Tree 写优化
 *
 * 核心原理:
 *   - MemTable: 内存中的有序写缓冲区 (当前实现用排序数组)
 *   - SSTable: 磁盘上的不可变有序文件块
 *   - Compaction: 将多个 SSTable 合并为更大的 SSTable，减少层数
 *   - Bloom Filter: 快速排除 SSTable 中不存在的 key
 *
 * Write path:  Put → MemTable → (flush when full) → Level 0 SSTable → Compaction → Level 1..N
 * Read path:   Get → MemTable → Bloom Filter check each SSTable → Binary search in SSTable
 */

typedef struct {
    int32_t key;
    int32_t value;
    bool    deleted;
} LSMMemEntry;

typedef struct {
    LSMMemEntry entries[LSM_MEMTABLE_MAX];
    int32_t     count;
} LSMMemTable;

typedef struct {
    int32_t min_key;
    int32_t max_key;
    int32_t keys[LSM_SSTABLE_MAX_ENTRIES];
    int32_t values[LSM_SSTABLE_MAX_ENTRIES];
    int32_t count;
    bool    deleted[LSM_SSTABLE_MAX_ENTRIES];
    uint8_t bloom_filter[LSM_BLOOM_BITS / 8];
} LSMSSTable;

typedef struct {
    LSMSSTable *tables[LSM_MAX_LEVELS];
    int32_t     table_counts[LSM_MAX_LEVELS];
    LSMMemTable memtable;
    int32_t     level_size_limit[LSM_MAX_LEVELS];
    int64_t     total_puts;
    int64_t     total_gets;
    int64_t     bloom_hits;
    int64_t     bloom_false_positives;
} LSMTree;

/* ---- Bloom Filter (L2: Core Concept - 概率数据结构) ----
 * m-bit filter, k hash functions.
 * 检查 key 是否绝对不在集合中 (no false negatives, possible false positives)
 */
void     bloom_init(uint8_t *filter, int32_t num_bits);
void     bloom_add(uint8_t *filter, int32_t num_bits, int32_t key);
bool     bloom_check(const uint8_t *filter, int32_t num_bits, int32_t key);

/* ---- LSM Tree API ---- */
void     lsm_init(LSMTree *tree);
bool     lsm_put(LSMTree *tree, int32_t key, int32_t value);
bool     lsm_get(LSMTree *tree, int32_t key, int32_t *value);
bool     lsm_delete(LSMTree *tree, int32_t key);
void     lsm_flush(LSMTree *tree);
void     lsm_compact(LSMTree *tree, int32_t level);
int32_t  lsm_get_level_count(const LSMTree *tree);
int64_t  lsm_get_total_bytes(const LSMTree *tree);
void     lsm_print_stats(const LSMTree *tree);

#endif
