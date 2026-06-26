# LSM-Tree Storage Engine — mini-nosql-store

> 参考 DynamoDB (Amazon), Bigtable (Google), LevelDB, RocksDB Internals — LSM-tree 的完整迷你实现

---

## 目录

1. [概述](#概述)
2. [LSM-Tree 架构](#lsm-tree-架构)
3. [核心组件详解](#核心组件详解)
4. [写入路径 (Write Path)](#写入路径-write-path)
5. [读取路径 (Read Path)](#读取路径-read-path)
6. [压缩策略 (Compaction)](#压缩策略-compaction)
7. [布隆过滤器 (Bloom Filter)](#布隆过滤器-bloom-filter)
8. [跳表 (Skip List) 实现](#跳表-skip-list-实现)
9. [SSTable 格式](#sstable-格式)
10. [性能特征](#性能特征)
11. [与工业级系统的对比](#与工业级系统的对比)
12. [数据结构详解](#数据结构详解)
13. [API 参考](#api-参考)
14. [调优参数](#调优参数)
15. [示例运行](#示例运行)
16. [进一步扩展](#进一步扩展)

---

## 概述

LSM-Tree (Log-Structured Merge-Tree) 是当代分布式数据库和高性能存储引擎的核心数据结构。几乎所有主流 NoSQL 数据库——Bigtable、DynamoDB、Cassandra、HBase、LevelDB、RocksDB——都基于 LSM-tree 或其变体。

**核心思想**：将随机写操作转换为顺序写操作，通过后台压缩（compaction）来优化读取性能。

### 为什么需要 LSM-Tree？

| 传统 B-Tree             | LSM-Tree                   |
|-------------------------|----------------------------|
| 原地更新 (in-place)     | 追加写 (append-only)        |
| 随机 I/O 写入           | 顺序 I/O 写入               |
| 页面分裂与重平衡        | 后台批量合并               |
| 写放大可控              | 写放大通过压缩管理         |
| 读取性能稳定            | 读取可能穿透多层           |

本实现提供了 LSM-tree 引擎的完整迷你版本，包括：
- **MemTable**: 基于跳表 (Skip List) 的内存表
- **Immutable MemTable**: 冻结的只读内存表
- **SSTable (Level-0)**: 第一层磁盘表，无序，可重叠
- **SSTable (Level-1)**: 第二层磁盘表，有序，不重叠
- **Bloom Filter**: 减少无效的 SSTable 读取
- **Compaction**: Level-0 至 Level-1 的简单压缩

---

## LSM-Tree 架构

```
写入数据
    │
    ▼
┌─────────────┐
│  MemTable   │  ← 活跃写入表 (跳表结构, O(log n) 插入)
│  (Active)   │
└──────┬──────┘
       │ 达到阈值 → 冻结
       ▼
┌─────────────┐
│  Immutable  │  ← 只读, 等待刷入磁盘
│  MemTable   │
└──────┬──────┘
       │ 序列化 → SSTable
       ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  SSTable    │  │  SSTable    │  │  SSTable    │  ← Level-0 (可重叠)
│  L0-S1.sst  │  │  L0-S2.sst  │  │  L0-S3.sst  │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                │                │
       └────────┬───────┴────────────────┘
                │ Compaction: Level-0 → Level-1
                ▼
┌──────────────────────────────────────────────┐
│  SSTable L1-S1.sst  (有序, 关键字不重叠)     │  ← Level-1
└──────────────────────────────────────────────┘
```

### 读取路径

```
查询请求
    │
    ├─► MemTable (最近数据)
    │   └─ 找到? → 返回
    │
    ├─► Immutable MemTables (从新到旧)
    │   └─ 找到? → 返回
    │
    ├─► Level-0 SSTables (Bloom Filter → 二分查找)
    │   └─ 找到? → 返回
    │
    └─► Level-1 SSTables (Bloom Filter → 二分查找)
        └─ 找到? → 返回 / NOT FOUND
```

---

## 核心组件详解

### 1. MemTable — 内存写入缓冲

MemTable 是 LSM-tree 写入的第一站。在 `mini-nosql-store` 中，MemTable 使用**跳表 (Skip List)** 实现。

**为什么用跳表？**
- 插入复杂度 O(log n)，与平衡树相当
- 实现比红黑树/AVL 树简单
- 天然支持有序遍历
- 并发控制更容易实现

```c
typedef struct memtable_t {
    SkipListNode *head;
    int           count;       // 当前条目数
    int           max_count;   // 达到后触发冻结
} MemTable;
```

**设计参数**：
- `LSM_MEMTABLE_MAX = 512`：达到此阈值时 MemTable 冻结
- `LSM_SKIPLIST_MAXLVL = 12`：跳表最大层级
- 每层晋升概率：1/4 (与 Redis 一致)

### 2. Immutable MemTable — 冻结表

当活跃 MemTable 达到 `max_count` 时，它被冻结为不可变的 Immutable MemTable。新写入转向新创建的 MemTable。

```c
int lsm_flush_memtable(LSMEngine *engine) {
    // 1. 将当前 MemTable 推入 immutable_memtables 数组
    engine->immutable_memtables[engine->imm_count++] = engine->memtable;
    // 2. 序列化 MemTable → SSTable (Level-0)
    SSTable *sst = sstable_from_memtable(engine->memtable, id, 0);
    engine->level0[engine->level0_count++] = sst;
    // 3. 创建新的 MemTable
    engine->memtable = memtable_create(LSM_MEMTABLE_MAX);
}
```

在工业级实现中（如 RocksDB），Immutable MemTable 被异步刷入磁盘，允许并发写入。

### 3. SSTable (Sorted String Table)

SSTable 是 LSM-tree 的磁盘存储单元。本实现包含：

```c
typedef struct sstable_t {
    int           id;
    int           level;              // 层级 (0 或 1)
    size_t        data_size;          // 数据块总大小
    char         *data_blocks;        // 原始数据块
    BloomFilter   bloom;              // 布隆过滤器
    int           key_count;          // 关键字数量
    char        (*keys)[LSM_MAX_KEY_LEN];  // 关键字数组
    int          *block_index_offsets;     // 块索引偏移
    int           index_size;         // 索引条目数
} SSTable;
```

**SSTable 特性**：
- Level-0 SSTable 的关键字范围可能重叠
- Level-1 SSTable 的关键字范围互不重叠
- 每个 SSTable 包含布隆过滤器，加速不存在查询

---

## 写入路径 (Write Path)

```
lsm_put(key, value)
    │
    ├─ memtable_put(mt, key, value)
    │   │
    │   └─ 跳表插入 (random level, O(log n))
    │
    ├─ 检查 memtable->count >= max_count?
    │   │
    │   └─ 是 → lsm_flush_memtable()
    │           │
    │           ├─ 冻结当前 MemTable → immutable
    │           ├─ 创建 SSTable (Level-0)
    │           ├─ 为 SSTable 构建布隆过滤器
    │           └─ 新建空 MemTable
    │
    └─ 返回成功
```

**写放大**：一次写入可能触发多次后续写入（冻结+压缩），这是 LSM-tree 的固有特征。通过调优 flush 阈值可以控制写放大。

---

## 读取路径 (Read Path)

```
lsm_get(key)
    │
    ├─ Step 1: memtable_get(active)
    │   └─ 跳表查找, O(log n)
    │
    ├─ Step 2: 遍历 immutable_memtables (从新到旧)
    │   └─ 跳表查找, O(log n) × imm_count
    │
    ├─ Step 3: 遍历 Level-0 SSTables (从新到旧)
    │   │
    │   ├─ bloom_check(sst, key) → false?
    │   │   └─ 跳过此 SSTable (布隆过滤器保证 key 不存在)
    │   │
    │   └─ bloom_check → true?
    │       └─ 在 keys 数组中二分查找 (O(log key_count))
    │
    ├─ Step 4: 遍历 Level-1 SSTables
    │   └─ 同 Step 3 (但 Level-1 关键字不重叠，可用二分定位)
    │
    ├─ 检查值是否为 TOMBSTONE ("__TOMBSTONE__")
    │   └─ 是 → 返回 NOT FOUND (键已删除)
    │
    └─ 返回结果
```

### 读放大

读放大 = MemTable 查找 + Σ(Immutable MemTable 查找) + Σ(Level-0 SSTable 查找) + Σ(Level-1 SSTable 查找)

布隆过滤器将大多数 SSTable 读取转换为 O(1) 的位图检查，大幅降低读放大。

---

## 压缩策略 (Compaction)

压缩是 LSM-tree 的核心维护操作，用于：
1. 移除过期/被覆盖/已删除的数据
2. 将 SSTable 从高层级合并到低层级
3. 减少读取时需要查询的 SSTable 数量

### 本实现的压缩

```c
int lsm_compact(LSMEngine *engine, int level) {
    if (level == 0 && engine->level0_count >= LSM_LEVEL0_MAX) {
        // 1. 创建新的 Level-1 SSTable
        SSTable *merged = sstable_create(next_id++, 1);
        // 2. 将所有 Level-0 SSTable 的关键字合并到 merged
        for (each Level-0 SSTable)
            for (each key in SSTable)
                sstable_add_key(merged, key, value);
        // 3. 添加到 Level-1
        engine->level1[level1_count++] = merged;
        // 4. 释放所有 Level-0 SSTable
        destroy_all_l0();
    }
}
```

### 压缩策略对比

| 策略          | 描述                                    | 适用场景          |
|---------------|-----------------------------------------|-------------------|
| Size-Tiered   | 按大小分层，同类大小合并 (Cassandra)     | 大吞吐写入        |
| Leveled       | 按层级组织，每层大小固定 (RocksDB)       | 低读放大          |
| Universal     | 全文件合并策略                          | 极高写入吞吐      |
| FIFO          | 先入先出，最老文件删除                  | 时序数据          |

本实现采用的是简化的 **Size-Tiered** 策略：当 Level-0 SSTable 数量达到阈值（4）时触发一次压缩，合并所有 Level-0 为单个 Level-1 SSTable。

---

## 布隆过滤器 (Bloom Filter)

布隆过滤器是一种概率性数据结构，用于测试一个元素是否属于一个集合。它可能返回假阳性（误报），但不会返回假阴性（漏报）。

### 实现

```c
#define LSM_BLOOM_BITS   1024   // 位图大小 (128 bytes)
#define LSM_BLOOM_HASHES 3      // 哈希函数数量

void bloom_add(BloomFilter *bf, const char *key) {
    // double-hashing: h(i, key) = (h1(key) + i * h2(key)) % BITS
    uint32_t h1 = djb2_hash(key);
    uint32_t h2 = sdbm_hash(key);
    for (int i = 0; i < LSM_BLOOM_HASHES; i++) {
        uint32_t pos = (h1 + i * h2) % LSM_BLOOM_BITS;
        bf->bits[pos / 8] |= (1 << (pos % 8));
    }
}

int bloom_check(BloomFilter *bf, const char *key) {
    // 任意一个位为 0 → 绝对不存在
    // 所有位都为 1 → 可能存在 (假阳性)
}
```

### 假阳性概率

对于布隆过滤器参数 m=1024 位, k=3 个哈希函数, n 个元素：

```
p(false_positive) ≈ (1 - e^(-kn/m))^k
```

| 元素数 n | 假阳性概率 |
|----------|-----------|
| 50       | ~0.002%   |
| 100      | ~0.08%    |
| 200      | ~2.1%     |
| 500      | ~24%      |

可见当 n 接近 m 时，假阳性率迅速上升。本实现中每个 SSTable 最多 512 个 key，布隆过滤器是足够的。

---

## 跳表 (Skip List) 实现

跳表是 MemTable 的核心数据结构。

### 结构

```
Level 3:  HEAD ─────────────────────────► 50 ──► NULL
Level 2:  HEAD ──────► 20 ──────────────► 50 ──► NULL
Level 1:  HEAD ──► 10 ──► 20 ──► 30 ──► 50 ──► NULL
Level 0:  HEAD ──► 10 ──► 17 ──► 20 ──► 25 ──► 30 ──► 37 ──► 50 ──► NULL
```

每个节点随机决定层级（概率 1/4 晋升到上一层），从而保证平均查找复杂度 O(log n)。

### 参数选择

- **P = 1/4**：Redis 使用的晋升概率
- **MAX_LEVEL = 12**：支持约 4^12 ≈ 1600 万条目的高效查找

---

## SSTable 格式

本实现的 SSTable 是内存中的简化版本：

```
┌─────────────────────────────────────┐
│  Block Index (offset array)         │
│  [0, 320, 640, 960, ...]            │
├─────────────────────────────────────┤
│  Key Array (sorted)                 │
│  [key:000000, key:000001, ...]      │
├─────────────────────────────────────┤
│  Bloom Filter (1024 bits)           │
│  [01101001...]                      │
├─────────────────────────────────────┤
│  Data Blocks (key+value pairs)      │
│  [value_data_000000, ...]           │
└─────────────────────────────────────┘
```

完整工业级格式还需要：
- **Data Block**：实际键值对数据，使用前缀压缩
- **Meta Block**：布隆过滤器
- **Meta Index Block**：元索引
- **Footer**：魔数 + 索引块位置

---

## 性能特征

### 写入性能

| 操作          | 复杂度             | 说明                          |
|---------------|-------------------|-------------------------------|
| lsm_put       | O(log n) 跳表插入 | n = MemTable 中的条目数        |
| lsm_flush     | O(n) 序列化       | 一次性操作                    |
| lsm_compact   | O(N) 全量合并     | N = 所有 Level-0 SSTable 总条目 |

### 读取性能

| 操作          | 最好情况          | 最坏情况                      |
|---------------|-------------------|-------------------------------|
| lsm_get (数据在 MemTable) | O(log n)           | O(log n)                     |
| lsm_get (数据在 SSTable)  | O(1) (bloom 过滤) | O(B × log K)                 |
| 不存在查询     | O(1) (bloom 过滤) | O(B × log K)                 |

其中 B = SSTable 数量, K = 每个 SSTable 的键数

---

## 与工业级系统的对比

| 特性              | mini-lsm-engine    | LevelDB         | RocksDB              | Cassandra              |
|-------------------|-------------------|-----------------|----------------------|------------------------|
| MemTable 结构      | Skip List         | Skip List       | Skip List / Hash     | Skip List / BTree      |
| SSTable 层级       | 2 (L0, L1)        | 7 (L0-L6)       | 7+ (L0-Ln)           | 1+ (L0-Ln)             |
| 压缩策略           | Size-Tiered       | Leveled         | Leveled / Universal   | Size-Tiered / Leveled  |
| 布隆过滤器         | 每 SSTable        | 每 SSTable      | 每 SSTable + 分区     | 每 SSTable             |
| 墓碑删除           | "__TOMBSTONE__"   | 特殊标记        | 特殊标记              | Tombstone              |
| 前缀压缩           | 不支持            | 支持            | 支持                  | 不支持                 |
| 校验和             | 不支持            | CRC32           | CRC32 / xxHash        | CRC32                  |
| 快照              | 不支持            | 支持            | 支持                  | 不支持                 |
| 写前日志 (WAL)     | 不支持            | 支持            | 支持                  | Commit Log             |

---

## 数据结构详解

### 跳表节点

```c
typedef struct skiplist_node_t {
    char  key[64];
    char  value[256];
    struct skiplist_node_t *forward[12];  // 多层前向指针
} SkipListNode;
```

### LSM 引擎状态

```c
typedef struct lsm_engine_t {
    MemTable  *memtable;              // 活跃写入
    MemTable **immutable_memtables;   // 冻结表列表
    int        imm_count;
    SSTable  **level0;               // Level-0 SSTables
    int        level0_count;
    SSTable  **level1;               // Level-1 SSTables
    int        level1_count;
    int        next_sstable_id;
    char       data_dir[256];
} LSMEngine;
```

---

## API 参考

### 核心 API

| 函数                          | 描述                                  |
|-------------------------------|---------------------------------------|
| `lsm_create(data_dir)`       | 创建 LSM 引擎实例                      |
| `lsm_destroy(engine)`        | 销毁引擎, 释放所有资源                 |
| `lsm_put(engine, k, v)`      | 写入键值对到 MemTable                 |
| `lsm_get(engine, k, out, sz)`| 按路径查找键值                        |
| `lsm_delete(engine, k)`      | 删除键 (写入墓碑)                     |
| `lsm_flush_memtable(engine)` | 冻结 MemTable → SSTable Level-0       |
| `lsm_compact(engine, level)`  | 触发指定层级的压缩                    |
| `lsm_compact_all(engine)`    | 触发全量压缩 (Level-0 → Level-1)      |
| `lsm_count(engine)`          | 返回总键数 (估算)                     |

### 辅助 API

| 函数                            | 描述                              |
|---------------------------------|-----------------------------------|
| `memtable_create(max)`          | 创建跳表 MemTable                |
| `memtable_put(mt, k, v)`        | 跳表插入                         |
| `memtable_get(mt, k, out, sz)`  | 跳表查找                         |
| `memtable_destroy(mt)`          | 释放跳表                         |
| `bloom_init(bf)`                | 初始化布隆过滤器                 |
| `bloom_add(bf, key)`            | 添加键到布隆过滤器               |
| `bloom_check(bf, key)`          | 检查键可能存在                   |
| `sstable_create(id, level)`     | 创建 SSTable                     |
| `sstable_get(sst, k, out, sz)`  | 在 SSTable 中查找               |
| `sstable_from_memtable(mt,id,l)`| MemTable → SSTable 转换          |

---

## 调优参数

| 参数                    | 默认 | 影响                            |
|-------------------------|------|----------------------------------|
| `LSM_MEMTABLE_MAX`      | 512  | 更大 → 更少 flush, 更多内存     |
| `LSM_LEVEL0_MAX`        | 4    | 更大 → 更少 compaction, 更多文件 |
| `LSM_BLOOM_BITS`       | 1024 | 更多 → 更低假阳性, 更多内存     |
| `LSM_BLOOM_HASHES`     | 3    | 更多 → 更低假阳性, 更多计算     |
| `LSM_SKIPLIST_MAXLVL`  | 12   | 适合最多 ~16M 条目              |
| `P (晋升概率)`           | 1/4  | 更高 → 更密集的索引, 更多指针   |

---

## 示例运行

编译并运行 LSM 引擎演示：

```bash
make
./build/lsm_demo
```

预期输出：

```
=== mini-nosql LSM-Tree Engine Demo ===

Putting 1000 keys into memtable...
Memtable count: 512

--- Testing GET from memtable ---
  GET key:000000 -> value_data_000000
  GET key:000100 -> value_data_000100

--- Flushing memtable to immutable + SSTable ---
Old memtable frozen (512 entries), new memtable created.
Immutable memtables: 1
Level-0 SSTables: 1

--- Testing GET after flush ---
  GET key:000000 -> value_data_000000 (found in Level-0)

--- Compaction ---
Level-0 SSTables: 0
Level-1 SSTables: 1
Total key count: ~1000
```

---

## 进一步扩展

为将此 mini LSM 引擎提升到生产级别，可以考虑：

1. **WAL (Write-Ahead Log)**
   - 在写入 MemTable 前先追加到日志文件
   - 崩溃恢复时重放 WAL 重建 MemTable

2. **多层级 (L2-L6)**
   - 实现完整的 leveled compaction 策略
   - 每层大小是上一层的 10 倍

3. **前缀压缩**
   - 相邻键共享前缀，减少存储空间
   - RocksDB 使用 `restart intervals` 压缩

4. **校验和**
   - 每个数据块添加 CRC32 校验和
   - 检测存储损坏

5. **合并迭代器**
   - 统一的迭代器接口遍历所有层级
   - 按键序合并多个有序源

6. **并发支持**
   - 读写锁保护的 MemTable
   - 无锁跳表 (lock-free skip list)

7. **持久化**
   - 将 SSTable 序列化到磁盘文件
   - MANIFEST 文件记录版本状态
