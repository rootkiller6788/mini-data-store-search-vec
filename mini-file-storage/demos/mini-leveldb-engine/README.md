# mini-leveldb-engine — LevelDB 内部架构深度解析

> 参考 LevelDB, RocksDB, ScyllaDB, Bitcask

---

## 目录

1. [概述](#概述)
2. [Memtable: 内存写入缓冲区](#memtable)
3. [SSTable: 磁盘上不可变的有序文件](#sstable)
4. [WAL (Write-Ahead Log): 崩溃恢复](#wal)
5. [Compaction: 合并压缩机制](#compaction)
6. [Bloom Filter: 快速过滤不存在的键](#bloom-filter)
7. [LSM-Tree 的整体写入与读取流程](#lsm-tree-写入与读取流程)
8. [LevelDB vs RocksDB vs ScyllaDB vs Bitcask](#对比分析)
9. [本实现 (mini-file-storage) 的设计说明](#本实现设计)

---

## 概述

LSM-Tree (Log-Structured Merge-Tree) 是一种广泛应用于现代存储引擎的数据结构。与传统的 B-Tree 不同，LSM-Tree 将随机写入转换为顺序写入，从而在高写入负载下获得优异的性能。

**核心思想**: 写入先进入内存中的排序结构 (Memtable)，当 Memtable 满时，将其刷写到磁盘成为不可变的 SSTable 文件。读取时需要查询多个层次，通过 Bloom Filter 和索引加速。后台 Compaction 线程负责合并小文件为大文件，减少读取时需要扫描的文件数量。

LevelDB 是 Google 开源的 LSM-Tree 实现，其设计影响深远。RocksDB 在 LevelDB 基础上做了大量优化。ScyllaDB 使用类似的思想但针对不同的工作负载。Bitcask 则是一种更简单的日志结构存储。

---

## Memtable

### 什么是 Memtable?

Memtable 是 LSM-Tree 的写入入口，是一个驻留在内存中的有序数据结构。所有写入首先进入 Memtable：
- 写入立即返回（假同步模式下）
- 数据按 key 排序存储
- 当 Memtable 大小达到阈值（通常是 4MB-64MB），它被标记为 "Immutable"（不可变）

### 数据结构选择

| 数据结构 | 优势 | 劣势 | 适用场景 |
|----------|------|------|----------|
| **Skip List** | 并发友好，实现简单，范围查询 O(n) | 内存占用较大（指针开销） | LevelDB 默认 |
| **红黑树/AVL** | 紧凑，查找 O(log n) | 并发控制复杂，重平衡开销 | 读多写少 |
| **Trie/ART** | 前缀压缩好 | 实现复杂 | 字符串 key |
| **Hash Map** | O(1) 写入/查找 | 不支持范围扫描 | 纯 KV 场景 |

LevelDB 使用 **Skip List**，因其在并发环境下表现良好（Skip List 的局部修改特性使得锁粒度更容易控制），且能高效支持范围查询。

### Memtable 的生命周期

```
写入 → Active Memtable
          ↓ (满了)
      Immutable Memtable
          ↓ (后台线程刷写)
      SSTable (Level 0 文件)
```

在一个 LSM 实例中，通常只维护 **1 个 Active Memtable** 和 **0~N 个 Immutable Memtable**（等待刷写到磁盘）。如果 Immutable Memtable 过多（写入太快），写操作会被阻塞（Write Stall）。

### Skip List 性能特征

| 操作 | 平均复杂度 | 最坏复杂度 |
|------|-----------|-----------|
| 插入 | O(log n) | O(n) |
| 查找 | O(log n) | O(n) |
| 删除 | O(log n) | O(n) |
| 有序遍历 | O(n) | - |
| 空间 | O(n log n) | O(n log n) |

LevelDB 中 Skip List 最多 12 层，每层概率 p=0.5（即每个节点有 50% 概率上升到更高层）。

**p=0.5 的含义**: 随机生成 level 时，while (random < 0.5) level++，这意味着：
- level=0: 100% 的节点
- level=1: 50% 的节点
- level=2: 25% 的节点
- ...
- level=11: 约 0.05% 的节点

---

## SSTable

### SSTable 文件格式

SSTable (Sorted String Table) 是 LSM-Tree 在磁盘上的基本存储单元。一个 SSTable 文件包含：

```
+─────────────────+
|   Data Block 0  |
+─────────────────+
|   Data Block 1  |
|      ...        |
|   Data Block N  |
+─────────────────+
|   Index Block   |
+─────────────────+
|  Bloom Filter   |
+─────────────────+
|     Footer      |  ← 固定 48 字节
+─────────────────+
```

### Data Block 内部结构

每个 Data Block（通常 4KB）包含：

```
+──────────+──────────+──────────────+────────────+
| Entry 0  | Entry 1  | ... | Entry K| Restart Ptrs|
+──────────+──────────+──────────────+────────────+
```

每个 Entry 记录一个 Key-Value 对：
```
shared_len (4B) | non_shared_len (4B) | value_len (4B) | key_delta | value
```

- **`shared_len`**: 与上一个 key 共享的前缀长度（前缀压缩）
- **`non_shared_len`**: 不同部分的长度
- **`value_len`**: 值的长度
- **`key_delta`**: key 的不同部分
- **`value`**: 值本身

**Restart Points**: 每 16 个 entry 设置一个"重启点"，在该点处 `shared_len=0`（完整 key 存储），之后 15 个 entry 使用增量编码。这样在查找时可以二分搜索重启点数组，然后顺序扫描最多 16 个 entry。

### Index Block

Index Block 存储每个 Data Block 的索引信息：

```
last_key (变长) | block_offset (4B) | block_size (4B)
```

`last_key` 是该 Block 中最后一个 key，`block_offset` 是 Block 在文件中的偏移，`block_size` 是 Block 的大小。

通过 Index Block，可以快速定位某个 key 可能在哪个 Data Block 中。

### Footer

Footer 固定 48 字节，位于文件末尾：

```
index_block_offset (4B) | index_block_size (4B) | magic_number (4B) | padding (36B)
```

`magic_number` (例如 0x88E2416B) 用于文件格式校验。读取时：
1. 跳到文件末尾 - 48 字节
2. 读取 Footer，验证 magic_number
3. 通过 index_block_offset 读取 Index Block
4. 在 Index Block 中二分搜索 → 找到 Data Block
5. 在 Data Block 中通过 Restart Points 二分 → 顺序扫描 → 找到 key

### 前缀压缩示例

假设我们有连续的 key: `"application_server"`, `"application_client"`, `"application_worker"`:

| Entry | shared_len | non_shared | full_key |
|-------|-----------|------------|----------|
| 0 (restart) | 0 | `application_server` | `application_server` |
| 1 | 12 (`application_`) | `client` | `application_client` |
| 2 | 12 (`application_`) | `worker` | `application_worker` |

前缀压缩可以显著减少存储空间，特别是对于有公共前缀的 key。

---

## WAL

### Write-Ahead Log 的作用

在将数据写入 Memtable 之前，先将操作记录写入 WAL 文件：

```
写入请求 → WAL (磁盘顺序写) → Memtable (内存) → [返回成功]
                         ↓
                  (如果崩溃)
                         ↓
                  WAL 重放 → 重建 Memtable
```

### WAL 格式

```
Record: checksum (4B) | length (4B) | type (1B) | key_len (4B) | value_len (4B) | key | value
```

- **checksum**: CRC32C 校验和，用于检测记录损坏
- **length**: payload 长度
- **type**: PUT (0x01) 或 DELETE (0x02)
- **key/value**: 实际的键值对

### 崩溃恢复流程

1. 打开 LSM Tree 时，检查 WAL 文件是否存在
2. 从 WAL 文件开头顺序读取记录
3. 验证每条记录的 checksum
4. 如果 checksum 不匹配，丢弃该记录及之后所有记录（可能部分写入）
5. 将有效记录重放到 Memtable 中
6. 完成恢复后，Seek 到 WAL 末尾继续正常写入

### WAL 旋转

LevelDB 在每次 Memtable 刷写后会关闭当前 WAL，创建新的 WAL 文件。旧的 WAL 文件可以被删除（因为其数据已持久化到 SSTable 中）。

---

## Compaction

### 为什么需要 Compaction?

随着写入增加，SSTable 文件不断累积：
- Level 0 的文件可能包含重叠的 key 范围
- 读取一个 key 可能需要搜索多个文件
- 删除的 key 占据空间（Tombstone）

Compaction 负责：
1. **合并小文件为大文件**
2. **去除过期的 key 版本**
3. **去除 Tombstone**（当底层没有对应 key 时）
4. **维持 Level 间的数据量比例**

### Leveled Compaction

LevelDB/RocksDB 默认使用 **Leveled Compaction**：

```
Level 0:  [SST] [SST] [SST] [SST]  ← 允许重叠, ≤4 个文件
            ↓ (compaction)
Level 1:  [SST         ] [SST         ]  ← 有序, 不重叠, 总大小 ≤10MB
            ↓
Level 2:  [                   ] [                   ]  ← 总大小 ≤100MB
            ↓
...
Level 6:  [                                                         ]  ← 最大层
```

**Level 0 → Level 1**: Level 0 文件数 > 4 时触发，选择 Level 0 中所有文件，与 Level 1 中 key 范围重叠的文件合并，输出到 Level 1。

**Level N → Level N+1**: Level N 总大小超过 `10^(N+1)` MB 时，选择一个文件，找到 N+1 层中 key 范围重叠的文件，合并后输出到 N+1 层。

### 写放大与读放大

| Compaction 策略 | 写放大 | 读放大 | 空间放大 |
|----------------|--------|--------|---------|
| Leveled | 高 (10-30x) | 低 | 低 (~1.1x) |
| Tiered/Universal | 低 (4-10x) | 高 | 高 (可能 2x) |
| FIFO | 极低 (1x) | 最高 | 可控 (TTL-based) |

**Leveled Compaction** 追求低读放大（每层最多 1 个文件包含目标 key），适合读密集型工作负载。

**Universal/Tiered Compaction** 追求低写放大（避免重复重写数据），适合写密集型工作负载（RocksDB, Cassandra 可选）。

### Compaction 统计指标

每次 Compaction 应该记录：
- 输入文件数
- 输出文件数
- 读取字节数
- 写入字节数
- 耗时 (ms)

---

## Bloom Filter

### 原理

Bloom Filter 是一种概率性数据结构，用于快速判断一个元素 **"肯定不存在"** 或 **"可能存在"**：

- **肯定不存在 (Definitely Not)**: Bloom Filter 说"没找到" → 100% 准确
- **可能存在 (Probably)**: Bloom Filter 说"可能有" → 有 false positive 概率

### 工作方式

1. 使用 k 个哈希函数（通常 3 个）
2. 添加 key 时，计算 k 个哈希值，将位图中对应位置 1
3. 查询 key 时，计算 k 个哈希值，检查位图中对应位是否全为 1
4. 如果有任何一位为 0 → 肯定不存在
5. 如果全为 1 → 可能存在（false positive rate 取决于 bits_per_key）

### 参数选择

False Positive Rate = (1 - e^(-k * n / m))^k

其中：
- n = 预期元素数量
- m = 位图大小 (bits)
- k = 哈希函数数量

**最佳 k = (m / n) * ln(2) ≈ 0.693 * (m / n)**

| bits_per_key | k=3 的 FPR | 内存开销 (n=10000) |
|-------------|-----------|------------------|
| 10 | ~0.82% | 12.5 KB |
| 14 | ~0.17% | 17.5 KB |
| 20 | ~0.0065% | 25 KB |

### 在 SSTable 中的应用

每个 SSTable 文件包含一个 Bloom Filter，存储在 Index Block 之后。当查找 key 时：
1. 先检查 Bloom Filter
2. 如果 Bloom Filter 说"不存在"，直接返回 Not Found
3. 节省了一次磁盘 I/O

---

## LSM-Tree 写入与读取流程

### 写入路径

```
客户端: put("foo", "bar")
    │
    ▼
┌─────────────┐
│ WAL Append   │ ← 顺序写入磁盘
└─────────────┘
    │
    ▼
┌─────────────┐
│  Memtable    │ ← Skip List Insert (O(log n))
└─────────────┘
    │
    │  Memtable full?
    ▼
┌─────────────────┐
│ Freeze Memtable │ ← 设为 Immutable
└─────────────────┘
    │
    ▼
┌──────────────────┐
│ Background Flush  │ ← 写入 SSTable (Level 0)
└──────────────────┘
    │
    ▼
┌──────────────────┐
│   Compaction     │ ← Level 0 → Level 1 → ... → Level 6
└──────────────────┘
```

### 读取路径

```
客户端: get("foo")
    │
    ▼
┌─────────────┐   找到? ──→ 返回值 / tombstone? → 返回不存在
│  Memtable    │
└─────────────┘   没找到?
    │
    ▼
┌────────────────┐   找到? ──→ 返回值...
│ Immutable (0)  │
└────────────────┘   没找到?
    │
    ▼
┌────────────────┐
│ Immutable (N)  │
└────────────────┘   没找到?
    │
    ▼
┌────────────────┐   Bloom → Maybe? → Index Search → Block Search
│ Level 0 SST    │   (检查所有 Level 0 文件, 最新的优先)
└────────────────┘   没找到?
    │
    ▼
┌────────────────┐   Bloom → Index Binary Search → Block Search
│ Level 1 SST    │   (只需检查 1 个文件, 因 Level 1 不重叠)
└────────────────┘
    │
    ...
    ▼
   返回 Not Found
```

**重要**: 搜索顺序为 Memtable → Immutable (newest first) → Level 0 (newest SST first) → Level 1 → ... → Level 6。在任意层找到 key 即停止，返回找到的第一个版本。

---

## 对比分析

### LevelDB vs RocksDB vs ScyllaDB vs Bitcask

| 特性 | LevelDB | RocksDB | ScyllaDB | Bitcask |
|------|---------|---------|----------|---------|
| **数据模型** | KV | KV | Wide-column | KV |
| **Memtable** | Skip List | Skip List / HashLinkList / Vector | Skip List (Memtable) | Hash Map (in-memory) |
| **WAL** | 单 WAL | Group WAL + Pipeline | Per-shard WAL | Hint File |
| **Compaction** | Leveled | Leveled / Universal / FIFO | Size-tiered (SSTable) | None (merge on read) |
| **Bloom Filter** | Per-SST | Full / Block-based | Per-SST | No (relies on OS cache) |
| **并发控制** | Mutex | Multi-threaded, Column Families | Shard-per-core | Single writer |
| **压缩** | Snappy | Zstd, LZ4, Snappy | LZ4 | None |
| **适用场景** | 嵌入式, 低并发 | 高并发, SSD 优化 | 分布式, 高吞吐写入 | 简单, 低容量写入 |

### Bitcask 的简洁之美

Bitcask (Riak 的存储引擎) 设计极其简单：
- 纯日志追加写入，不排序
- 内存中维护一个 Hash Map: key → (file_id, offset, size)
- 读取只需一次磁盘寻址
- 无 Compaction（后台 Merge 操作可选）
- 缺点：所有 key 必须在内存中，不支持范围扫描

---

## 本实现设计

`mini-file-storage` 是一个**教育用途**的 LSM-Tree 存储引擎简化实现：

### 架构

```
include/
├── sstable.h      — SSTable 文件格式定义
├── lsm_tree.h     — LSM Tree 引擎
├── skiplist.h     — 跳表 (Memtable)
├── wal_file.h     — 预写日志
└── compaction.h   — 压缩策略

src/
├── sstable.c      — SSTable 读写实现 (230+ 行)
├── lsm_tree.c     — LSM 引擎 (250+ 行)
├── skiplist.c     — 跳表实现 (150+ 行)
├── wal_file.c     — WAL 实现 (160+ 行)
└── compaction.c   — 压缩实现 (140+ 行)
```

### 关键简化

相对于完整的 LevelDB/RocksDB 实现，本项目做了以下简化：
1. **单线程**: 无并发控制，无 Mutex
2. **简化 WAL**: 单文件，无 Block 对齐
3. **无缓存**: 不使用 Block Cache，每次读取都从磁盘加载
4. **固定大小**: Key ≤ 256 字节, Value ≤ 1024 字节
5. **无压缩**: 不实现 Snappy/Zstd 压缩
6. **简化 Compaction**: 仅演示 Leveled 和 Tiered 基本策略

### 学习建议

1. 先阅读 `skiplist.h/c` — 理解 Memtable 数据结构
2. 再阅读 `sstable.h/c` — 理解磁盘文件格式
3. 接着阅读 `wal_file.h/c` — 理解崩溃恢复
4. 然后阅读 `compaction.h/c` — 理解合并策略
5. 最后阅读 `lsm_tree.h/c` — 将所有组件串联

运行示例程序验证理解：
```bash
# SSTable 读写 + Bloom Filter
./examples/sstable_demo

# Skip List 测试
./examples/skiplist_demo

# 完整 LSM-Tree 流程
./examples/lsm_demo
```

---

## 参考资料

- [LevelDB 源码](https://github.com/google/leveldb)
- [RocksDB Wiki](https://github.com/facebook/rocksdb/wiki)
- [ScyllaDB Architecture](https://docs.scylladb.com/stable/architecture/)
- [Bitcask Paper (Basho)](https://riak.com/assets/bitcask-intro.pdf)
- [BigTable Paper (Google, 2006)](https://static.googleusercontent.com/media/research.google.com/en//archive/bigtable-osdi06.pdf)
- [LSM-Tree Paper (O'Neil et al., 1996)](https://www.cs.umb.edu/~poneil/lsmtree.pdf)
- [WiscKey: Separating Keys from Values (FAST'16)](https://www.usenix.org/system/files/conference/fast16/fast16-papers-lu.pdf)
- [Designing Data-Intensive Applications (Kleppmann)](https://dataintensive.net/)

---

*mini-file-storage — 一个用于学习的 LSM-Tree 存储引擎实现*
