#include "lsm_tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L8: LSM Tree (Log-Structured Merge Tree) 实现
 *
 * LSM Tree 是现代写优化存储引擎的核心数据结构。由 O'Neil 等人于 1996 年
 * 在论文 "The Log-Structured Merge-Tree (LSM-Tree)" 中提出。
 *
 * 核心思想 (Law of LSM Tree):
 *   - 随机写 → 顺序写: 所有写入首先进入内存 MemTable，然后批量刷到磁盘 SSTable
 *   - 牺牲读性能换写性能: 读取可能需要搜索多个 SSTable
 *   - Compaction: 后台合并多个小 SSTable 为大 SSTable，减少读放大
 *
 * 使用本结构的系统: LevelDB, RocksDB, Cassandra, HBase, WiredTiger (MongoDB)
 *
 * 课程映射: CMU 15-445 Lecture 09 (LSM Trees)
 *            Berkeley CS 186 (Data Structures and Algorithms for DB)
 *            CMU 15-721 (Advanced Database Systems) — LSM Tree optimizations
 *
 * 与 B+Tree 的对比 (L4: Trade-off Theorem):
 *   ┌──────────┬─────────────────┬──────────────────┐
 *   │          │ B+Tree          │ LSM Tree         │
 *   ├──────────┼─────────────────┼──────────────────┤
 *   │ 写放大   │ O(log_B N) R/W  │ O(K/B) seq write │
 *   │ 读放大   │ O(log_B N)      │ O(K * log N)     │
 *   │ 空间放大 │ 50%-100%        │ 10%-30%          │
 *   │ 优化场景 │ OLTP (读写混合)  │ OLAP/TSDB (写多)  │
 *   └──────────┴─────────────────┴──────────────────┘
 */

/* ---- Bloom Filter ----
 * L2: 概率数据结构 (Probabilistic Data Structure)
 *
 * Bloom Filter 回答: "这个 key 是否绝对不在这个 SSTable 中？"
 *   - No false negatives: 如果 filter 说 NO → 100% 不存在
 *   - Possible false positives: 如果 filter 说 YES → 可能不存在 (需要实际查找)
 *
 * 参数: m bits 空间, k 个哈希函数
 * 假阳性概率: p ≈ (1 - e^(-kn/m))^k
 *
 * 本实现使用 m = 1024, k = 4 → p ≈ 2.4% (10 entries), 15% (100 entries)
 * 优化: 使用双重哈希 (Kirsch-Mitzenmacher 方法) 从两个hash生成k个
 *       h_i(x) = h1(x) + i * h2(x)
 *
 * 课程映射: MIT 6.006 (Introduction to Algorithms) — Hashing & Bloom Filters
 *            Berkeley CS 61B (Data Structures)
 */

static uint32_t bloom_hash1(int32_t key) {
    /* FNV-1a hash — simple and effective for small keys */
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)&key;
    for (int32_t i = 0; i < (int32_t)sizeof(key); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t bloom_hash2(int32_t key) {
    /* Murmur-inspired hash for independence from hash1 */
    uint32_t h = (uint32_t)key;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

void bloom_init(uint8_t *filter, int32_t num_bits) {
    memset(filter, 0, ((size_t)num_bits + 7) / 8);
}

void bloom_add(uint8_t *filter, int32_t num_bits, int32_t key) {
    uint32_t h1 = bloom_hash1(key);
    uint32_t h2 = bloom_hash2(key);
    for (int32_t i = 0; i < LSM_BLOOM_HASHES; i++) {
        uint32_t h = (h1 + (uint32_t)i * h2) % (uint32_t)num_bits;
        filter[h / 8] |= (uint8_t)(1u << (h % 8));
    }
}

bool bloom_check(const uint8_t *filter, int32_t num_bits, int32_t key) {
    uint32_t h1 = bloom_hash1(key);
    uint32_t h2 = bloom_hash2(key);
    for (int32_t i = 0; i < LSM_BLOOM_HASHES; i++) {
        uint32_t h = (h1 + (uint32_t)i * h2) % (uint32_t)num_bits;
        if (!(filter[h / 8] & (1u << (h % 8)))) return false;
    }
    return true;
}

/* ---- MemTable ----
 * L8: MemTable — 内存中的有序写缓冲区
 *
 * 本实现使用排序数组作为最简单的 MemTable 结构。
 * 工业实现通常使用:
 *   - Skip List: LevelDB, RocksDB (默认)
 *   - Red-Black Tree: WiredTiger
 *   - B+Tree: 某些实现
 *
 * 本简单实现每次插入都保持有序 (array insertion sort),
 * 复杂度 O(n) per insert, 适合教学目的。
 * 
 * MemTable 满时触发 flush → 生成新的 Level-0 SSTable
 */

static void memtable_init(LSMMemTable *mt) {
    mt->count = 0;
}

/* 在已排序数组中二分查找 key 的插入位置 */
static int32_t memtable_find_key(const LSMMemTable *mt, int32_t key) {
    int32_t lo = 0, hi = mt->count;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (mt->entries[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* 插入到 MemTable 的排序位置 */
static bool memtable_put(LSMMemTable *mt, int32_t key, int32_t value) {
    if (mt->count >= LSM_MEMTABLE_MAX) return false;
    int32_t pos = memtable_find_key(mt, key);

    /* 检查是否已存在 (更新 value) */
    if (pos < mt->count && mt->entries[pos].key == key) {
        mt->entries[pos].value = value;
        mt->entries[pos].deleted = false;
        return true;
    }

    /* 后移元素 */
    for (int32_t i = mt->count; i > pos; i--) {
        mt->entries[i] = mt->entries[pos];
    }
    /* 实际上应该 memmove, 但写成循环更直观 */
    for (int32_t i = mt->count - 1; i >= pos; i--) {
        mt->entries[i + 1] = mt->entries[i];
    }

    mt->entries[pos].key = key;
    mt->entries[pos].value = value;
    mt->entries[pos].deleted = false;
    mt->count++;
    return true;
}

/* 从 MemTable 中查找 key (二分搜索, O(log n)) */
static bool memtable_get(const LSMMemTable *mt, int32_t key, int32_t *value) {
    int32_t pos = memtable_find_key(mt, key);
    if (pos >= mt->count || mt->entries[pos].key != key) return false;
    if (mt->entries[pos].deleted) return false;
    *value = mt->entries[pos].value;
    return true;
}

/* ---- SSTable ----
 * L8: SSTable (Sorted String Table) — 磁盘上的不可变有序文件
 *
 * 特点:
 *   - 一旦创建不再修改 (Immutable)
 *   - 包含 Bloom Filter 用于快速排除
 *   - 包含 min_key/max_key 用于范围检查
 *   - 内部用排序数组表示 (真实系统用跳表或分块索引)
 *
 * 读取流程: Bloom Filter → min/max check → Binary Search
 */

static void sstable_init(LSMSSTable *sst) {
    memset(sst, 0, sizeof(*sst));
    sst->min_key = INT32_MAX;
    sst->max_key = INT32_MIN;
    bloom_init(sst->bloom_filter, LSM_BLOOM_BITS);
}

/* SSTable 中二分查找 key */
static bool sstable_search(const LSMSSTable *sst, int32_t key, int32_t *value) {
    /* 快速范围检查: 排除 key 不在 [min, max] 范围内的情况 */
    if (key < sst->min_key || key > sst->max_key) return false;

    /* Bloom Filter 快速排除 */
    if (!bloom_check(sst->bloom_filter, LSM_BLOOM_BITS, key)) {
        return false;
    }

    /* 二分搜索 */
    int32_t lo = 0, hi = sst->count;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (sst->keys[mid] < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo < sst->count && sst->keys[lo] == key) {
        if (sst->deleted[lo]) return false;  /* tombstone */
        *value = sst->values[lo];
        return true;
    }
    return false;
}

/* ---- LSM Tree API ---- */

void lsm_init(LSMTree *tree) {
    memset(tree, 0, sizeof(*tree));
    memtable_init(&tree->memtable);

    /* Level size limits: 每层最多容纳的 SSTable 数量
     * Level 0: 4, Level 1: 8, Level 2: 16 ... 等比增长
     */
    for (int32_t i = 0; i < LSM_MAX_LEVELS; i++) {
        tree->level_size_limit[i] = 4 * (1 << i);
    }
}

/*
 * L5: LSM Put 算法
 *
 * Write Path:
 *   Put(key, value) → MemTable
 *   if MemTable is full:
 *     flush MemTable → new Level-0 SSTable
 *     if Level-0 has too many SSTables → compact Level-0 → Level-1
 *
 * 复杂度: O(MT_size) per insert (本教学实现)
 *         工业实现: O(log MT_size) with Skip List
 */
bool lsm_put(LSMTree *tree, int32_t key, int32_t value) {
    if (!tree) return false;
    tree->total_puts++;

    /* 尝试放入 MemTable */
    if (!memtable_put(&tree->memtable, key, value)) {
        /* MemTable 满: 先 flush 到 Level 0, 再重试 */
        lsm_flush(tree);
        if (!memtable_put(&tree->memtable, key, value)) {
            return false;
        }
    }
    return true;
}

/*
 * L5: LSM Get 算法
 *
 * Read Path:
 *   1. 先搜索 MemTable (最新数据)
 *   2. 从 Level 0 到 Level N 搜索每个 SSTable
 *   3. 每个 SSTable: Bloom Filter → Range Check → Binary Search
 *   4. 第一个找到的非删除条目即为最新版本
 *   5. 遇到 tombstone (deleted=true) 表示 key 已删除
 *
 * 复杂度: O(K * log(SST_size)) where K = number of SSTables checked
 *         加上 Bloom Filter 假阳性可能导致额外 SSTable 检查
 */
bool lsm_get(LSMTree *tree, int32_t key, int32_t *value) {
    if (!tree || !value) return false;
    tree->total_gets++;

    /* Step 1: Search MemTable */
    if (memtable_get(&tree->memtable, key, value)) {
        return true;
    }

    /* Check if key is tombstoned in memtable */
    int32_t pos = memtable_find_key(&tree->memtable, key);
    if (pos < tree->memtable.count && 
        tree->memtable.entries[pos].key == key &&
        tree->memtable.entries[pos].deleted) {
        return false;
    }

    /* Step 2: Search SSTables from Level 0 to N */
    for (int32_t level = 0; level < LSM_MAX_LEVELS; level++) {
        for (int32_t t = 0; t < tree->table_counts[level]; t++) {
            LSMSSTable *sst = &tree->tables[level][t];

            /* Bloom filter check — count statistics */
            bool in_bloom = bloom_check(sst->bloom_filter, LSM_BLOOM_BITS, key);
            if (!in_bloom) {
                /* Correct filter rejection — no false negative */
                continue;
            }

            if (sstable_search(sst, key, value)) {
                tree->bloom_hits++;
                return true;
            }

            /* Key not in SSTable but bloom said yes → false positive */
            tree->bloom_false_positives++;
        }
    }

    return false;
}

/*
 * L5: LSM Delete 算法
 *
 * LSM Tree 的删除是逻辑删除 (Tombstone):
 *   插入一个 deleted=true 的条目
 *   在 Compaction 时如果所有更高级别都删除了该 key，才真正删除
 */
bool lsm_delete(LSMTree *tree, int32_t key) {
    if (!tree) return false;
    tree->total_puts++;

    /* 在 MemTable 中标记为已删除 (tombstone marker) */
    int32_t pos = memtable_find_key(&tree->memtable, key);
    if (pos < tree->memtable.count && tree->memtable.entries[pos].key == key) {
        tree->memtable.entries[pos].deleted = true;
        return true;
    }

    /* Key not in MemTable: insert a tombstone entry */
    if (tree->memtable.count >= LSM_MEMTABLE_MAX) {
        lsm_flush(tree);
    }
    if (tree->memtable.count < LSM_MEMTABLE_MAX) {
        LSMMemEntry *e = &tree->memtable.entries[tree->memtable.count];
        e->key = key;
        e->value = 0;
        e->deleted = true;

        /* Insert in sorted order */
        int32_t ins_pos = memtable_find_key(&tree->memtable, key);
        for (int32_t i = tree->memtable.count; i > ins_pos; i--) {
            tree->memtable.entries[i] = tree->memtable.entries[i - 1];
        }
        tree->memtable.entries[ins_pos] = *e;
        tree->memtable.count++;
        return true;
    }
    return false;
}

/* ---- Flush MemTable → Level-0 SSTable ----
 * L5: Flush 算法
 *
 * 将内存中的 MemTable 转储为 Level-0 的一个新的不可变 SSTable。
 * Level-0 的 SSTable 之间可能有 key 范围重叠。
 *
 * 工业实践: LevelDB/RocksDB 允许 Level-0 重叠，Level-1+ 不重叠
 */
void lsm_flush(LSMTree *tree) {
    if (!tree || tree->memtable.count == 0) return;

    /* 检查 Level-0 是否有空间 */
    if (tree->table_counts[0] >= tree->level_size_limit[0]) {
        lsm_compact(tree, 0);
    }

    /* 分配新的 SSTable */
    if (tree->tables[0] == NULL) {
        tree->tables[0] = (LSMSSTable *)calloc(
            (size_t)tree->level_size_limit[0], sizeof(LSMSSTable));
        if (!tree->tables[0]) return;
    }

    int32_t idx = tree->table_counts[0];
    LSMSSTable *sst = &tree->tables[0][idx];
    sstable_init(sst);

    /* 复制 MemTable 数据到 SSTable */
    sst->count = tree->memtable.count;
    sst->min_key = sst->count > 0 ? tree->memtable.entries[0].key : 0;
    sst->max_key = sst->count > 0 ? 
        tree->memtable.entries[sst->count - 1].key : 0;

    for (int32_t i = 0; i < sst->count && i < LSM_SSTABLE_MAX_ENTRIES; i++) {
        sst->keys[i] = tree->memtable.entries[i].key;
        sst->values[i] = tree->memtable.entries[i].value;
        sst->deleted[i] = tree->memtable.entries[i].deleted;
        bloom_add(sst->bloom_filter, LSM_BLOOM_BITS, sst->keys[i]);
    }
    if (sst->count > LSM_SSTABLE_MAX_ENTRIES) {
        sst->count = LSM_SSTABLE_MAX_ENTRIES;
    }

    tree->table_counts[0]++;
    memtable_init(&tree->memtable);
}

/*
 * L5: Compaction 算法
 *
 * Compaction 将 Level-i 的所有 SSTable 合并到 Level-(i+1)。
 * 这是 LSM Tree 的核心维护操作:
 *   1. 读取 Level i 和 Level i+1 的所有条目
 *   2. 合并排序 (归并)
 *   3. 丢弃过时的版本 (同一 key 只保留最新)
 *   4. 写回 Level i+1，清空 Level i
 *
 * 复杂度: O((N_i + N_{i+1}) * log(N_i + N_{i+1}))
 * 对应 LevelDB 的 Minor Compaction 概念
 *
 * 课程映射: CMU 15-721 — LSM Compaction strategies (Tiering vs Leveling)
 */
void lsm_compact(LSMTree *tree, int32_t level) {
    if (!tree || level >= LSM_MAX_LEVELS - 1) return;
    if (tree->table_counts[level] == 0) return;

    /* 确保 level+1 有空间 */
    int32_t sz = tree->level_size_limit[level + 1];
    if (tree->tables[level + 1] == NULL) {
        tree->tables[level + 1] = (LSMSSTable *)calloc((size_t)sz, sizeof(LSMSSTable));
    }
    if (!tree->tables[level + 1]) return;

    /* Step 1: 收集所有从 level 和 level+1 的条目到临时数组 */
    int32_t max_entries = (tree->table_counts[level] + tree->table_counts[level + 1]) 
                          * LSM_SSTABLE_MAX_ENTRIES;
    LSMMemEntry *all = (LSMMemEntry *)malloc(sizeof(LSMMemEntry) * (size_t)max_entries);
    if (!all) return;
    int32_t all_count = 0;

    /* 从 level+1 收集 (旧数据, 优先被覆盖) */
    for (int32_t t = 0; t < tree->table_counts[level + 1]; t++) {
        LSMSSTable *sst = &tree->tables[level + 1][t];
        for (int32_t j = 0; j < sst->count && all_count < max_entries; j++) {
            all[all_count].key = sst->keys[j];
            all[all_count].value = sst->values[j];
            all[all_count].deleted = sst->deleted[j];
            all_count++;
        }
    }

    /* 从 level 收集 (新数据, 最新版本) */
    for (int32_t t = 0; t < tree->table_counts[level]; t++) {
        LSMSSTable *sst = &tree->tables[level][t];
        for (int32_t j = 0; j < sst->count && all_count < max_entries; j++) {
            all[all_count].key = sst->keys[j];
            all[all_count].value = sst->values[j];
            all[all_count].deleted = sst->deleted[j];
            all_count++;
        }
    }

    /* Step 2: 排序所有条目 (按 key, 相同 key 按插入顺序——后来的在前面) */
    for (int32_t i = 0; i < all_count - 1; i++) {
        for (int32_t j = i + 1; j < all_count; j++) {
            if (all[i].key > all[j].key) {
                LSMMemEntry tmp = all[i];
                all[i] = all[j];
                all[j] = tmp;
            }
        }
    }

    /* Step 3: 去重 — 同一 key 保留最新版本 (数组中靠前的) */
    int32_t unique = 0;
    for (int32_t i = 1; i < all_count; i++) {
        if (all[i].key != all[unique].key) {
            unique++;
            all[unique] = all[i];
        } else {
            /* 同 key: 前面的是较新版本，跳过当前 */
            /* 但如果前面的标记为 deleted 而当前有数据，用当前 */
            if (all[unique].deleted && !all[i].deleted) {
                all[unique] = all[i];
            }
        }
    }
    unique++;

    /* Step 4: 写入新的 Level+1 SSTables */
    free(tree->tables[level + 1]);
    tree->tables[level + 1] = (LSMSSTable *)calloc((size_t)sz, sizeof(LSMSSTable));
    tree->table_counts[level + 1] = 0;

    int32_t entries_per_sst = LSM_SSTABLE_MAX_ENTRIES;
    int32_t num_new_ssts = (unique + entries_per_sst - 1) / entries_per_sst;
    if (num_new_ssts > sz) num_new_ssts = sz;

    for (int32_t s = 0; s < num_new_ssts; s++) {
        LSMSSTable *sst = &tree->tables[level + 1][s];
        sstable_init(sst);
        int32_t start = s * entries_per_sst;
        int32_t end = (start + entries_per_sst) < unique ? (start + entries_per_sst) : unique;
        sst->count = end - start;
        if (sst->count > 0) {
            sst->min_key = all[start].key;
            sst->max_key = all[end - 1].key;
        }
        for (int32_t j = start; j < end; j++) {
            int32_t idx = j - start;
            sst->keys[idx] = all[j].key;
            sst->values[idx] = all[j].value;
            sst->deleted[idx] = all[j].deleted;
            bloom_add(sst->bloom_filter, LSM_BLOOM_BITS, sst->keys[idx]);
        }
        tree->table_counts[level + 1]++;
    }

    /* Step 5: 清空 Level i */
    if (tree->tables[level]) {
        free(tree->tables[level]);
        tree->tables[level] = NULL;
    }
    tree->table_counts[level] = 0;

    free(all);
}

/* ---- Statistics & Utility ---- */

int32_t lsm_get_level_count(const LSMTree *tree) {
    if (!tree) return 0;
    int32_t levels = 0;
    for (int32_t i = 0; i < LSM_MAX_LEVELS; i++) {
        if (tree->table_counts[i] > 0) levels = i + 1;
    }
    return levels;
}

int64_t lsm_get_total_bytes(const LSMTree *tree) {
    if (!tree) return 0;
    int64_t total = 0;
    /* MemTable */
    total += (int64_t)(tree->memtable.count * (int32_t)sizeof(LSMMemEntry));
    /* SSTables */
    for (int32_t l = 0; l < LSM_MAX_LEVELS; l++) {
        total += (int64_t)tree->table_counts[l] * (int64_t)sizeof(LSMSSTable);
    }
    return total;
}

void lsm_print_stats(const LSMTree *tree) {
    if (!tree) return;
    printf("=== LSM Tree Statistics ===\n");
    printf("Puts: %lld, Gets: %lld\n",
           (long long)tree->total_puts, (long long)tree->total_gets);
    printf("Bloom hits: %lld, false positives: %lld\n",
           (long long)tree->bloom_hits, (long long)tree->bloom_false_positives);
    printf("Levels: %d\n", lsm_get_level_count(tree));
    for (int32_t l = 0; l < LSM_MAX_LEVELS; l++) {
        if (tree->table_counts[l] > 0) {
            printf("  Level %d: %d SSTables\n", l, tree->table_counts[l]);
            for (int32_t t = 0; t < tree->table_counts[l] && t < 5; t++) {
                LSMSSTable *sst = &tree->tables[l][t];
                printf("    SSTable[%d]: %d entries, range=[%d, %d]\n",
                       t, sst->count, sst->min_key, sst->max_key);
            }
            if (tree->table_counts[l] > 5) {
                printf("    ... (%d more)\n", tree->table_counts[l] - 5);
            }
        }
    }
    printf("MemTable: %d entries\n", tree->memtable.count);
    printf("Total bytes: %lld\n", (long long)lsm_get_total_bytes(tree));
}