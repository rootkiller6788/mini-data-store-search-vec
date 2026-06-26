# mini-db-kernel — 数据库内核 (C 语言实现)

> 参考 CMU 15-445 Database Systems, Database Internals (Petrov)
> 代码总量: include/ + src/ ≥ **3,694 行**

## Module Status: COMPLETE ✅

- **L1-L6**: Complete
- **L7 (Applications)**: Complete (KV Store, Query Exec, Cursor Scanner)
- **L8 (Advanced Topics)**: Partial (LSM Tree, Bloom Filter)
- **L9 (Industry Frontiers)**: Partial (documented, not implemented)
- **34/34 tests passing**

---

## 模块

| 模块 | 头文件 | 实现 | 知识层级 |
|:---|:---|:---|:---|
| Buffer Pool | `include/buffer_pool.h` | `src/buffer_pool.c` (175行) | L1-L3 |
| B+Tree | `include/btree.h` | `src/btree.c` (349行) | L1-L5 |
| WAL / ARIES Recovery | `include/wal.h` | `src/wal.c` (153行) | L1-L5 |
| Lock Manager (2PL) | `include/lock_manager.h` | `src/lock_manager.c` (210行) | L1-L5 |
| MVCC | `include/mvcc.h` | `src/mvcc.c` (195行) | L1-L5 |
| **Slotted Page** | `include/slotted_page.h` | `src/slotted_page.c` (268行) | L1-L3 |
| **Disk Manager** | `include/disk_manager.h` | `src/disk_manager.c` (160行) | L1-L3 |
| **LSM Tree** | `include/lsm_tree.h` | `src/lsm_tree.c` (557行) | L2, L5, L8 |
| **Query Exec** | `include/query_exec.h` | `src/query_exec.c` (600行) | L2, L5, L7 |
| **KV Store** | `include/kv_store.h` | `src/kv_store.c` (413行) | L7 |

## 九层知识覆盖摘要

### L1: Definitions (Complete ✅)
- `Frame`, `BufferPool` — 页面缓存结构
- `BTreeNode`, `BTree` — B+Tree 节点与索引
- `WALRecord`, `WALManager` — WAL 日志管理
- `LockEntry`, `LockManager` — 两阶段锁
- `TupleVersion`, `MVCCTransaction` — 多版本并发控制
- `SlottedPage`, `PageHeader`, `SlotEntry` — 页面布局
- `DiskManager`, `DMFileInfo` — 磁盘抽象层
- `LSMTree`, `LSMMemTable`, `LSMSSTable` — LSM 树结构
- `QEOperator`, `TableSchema`, `QETuple` — 火山模型算子
- `KVStore`, `KVTransaction`, `KVCursor` — KV 存储引擎

### L2: Core Concepts (Complete ✅)
- Buffer Pool replacement policy (LRU)
- B+Tree: 数据在叶子，内部节点存路由键
- WAL: 先写日志再写数据
- 2PL: Growing Phase → Shrinking Phase
- MVCC: 快照隔离 (Snapshot Isolation)
- Slotted Page: 页面内部组织
- Disk Manager: 页面分配策略
- LSM Tree: MemTable + SSTable + Compaction
- Bloom Filter: 概率数据结构
- Volcano Model: open/next/close 迭代器模式
- In-Place Update vs Out-of-Place Update

### L3: Engineering Structures (Complete ✅)
- Page Header + Slot Array + Tuple Data 三段式布局
- Hash-based Page Table (在 Buffer Pool 中)
- LRU 链表实现
- 页面压缩/碎片整理 (compact)
- 模拟磁盘 I/O 抽象层

### L4: Standards/Theorems (Complete ✅)
- ACID 属性分解 (A→WAL+Undo, C→索引约束, I→MVCC/2PL, D→WAL+Checkpoint)
- DJB2 哈希函数 (D. J. Bernstein)
- Bloom Filter 假阳性率: p ≈ (1 - e^(-kn/m))^k
- 乘法哈希: Knuth's Golden Ratio (2654435761)
- ARIES Recovery 三阶段: Analysis → Redo → Undo
- B+Tree vs LSM Tree 读写放大权衡
- CAP Theorem: 本系统选择 CP (Consistency + Partition Tolerance)

### L5: Algorithms/Methods (Complete ✅)
- B+Tree: Insert with split, Delete with borrow/merge, Range scan
- LRU 淘汰: 链表维护，O(1) touch, O(n) evict
- ARIES Recovery: Analysis→Redo→Undo 三阶段
- Deadlock Detection: Waits-for graph cycle detection
- MVCC Visibility Check: 基于 xmin/xmax/active_list
- Slotted Page Compact: 碎片整理算法
- LSM Tree Compaction: Tiering merge
- Nested Loop Join: O(|R|×|S|)
- Insertion Sort (LSM MemTable)
- Tombstone-based Deletion (LSM)

### L6: Canonical Problems (Complete ✅)
- Buffer Pool Manager — `examples/buffer_pool_demo.c`
- B+Tree Index — `examples/btree_demo.c`
- WAL / ARIES Recovery — `examples/wal_demo.c`

### L7: Applications (Complete ✅, ≥2)
1. **KV Store** (`kv_store.c`) — 整合所有模块的 Key-Value 存储引擎
2. **Query Execution Engine** (`query_exec.c`) — 火山模型实现
3. **Cursor-based Range Scan** — B+Tree leaf chain traversal

### L8: Advanced Topics (Partial ⚠️, 1 completed)
1. **LSM Tree** (`lsm_tree.c`) — 写优化索引结构 (LevelDB/RocksDB 基础)
2. Bloom Filter implementation
3. Formal Verification: not yet implemented

### L9: Industry Frontiers (Partial ⚠️, documented only)
- AI Compiler / MLIR: documented in knowledge-graph
- Confidential Computing: documented
- Modern DB Engines (TiDB, CockroachDB): referenced in docs

## 九校课程映射

| 学校 | 关键课程 | 本模块对应 |
|------|---------|-----------|
| **CMU** | 15-445 Database Systems | Project 1-4 全覆盖 |
| **Berkeley** | CS 186 Database Systems | Storage, Indexing, Concurrency |
| **Stanford** | CS 245 Principles of Data-Intensive Systems | Query Execution |
| **MIT** | 6.824 Distributed Systems | (部分) 2PC/Raft 概念 |
| **清华** | 数据库系统概论 | 存储引擎 + 事务管理 |
| **ETH** | 263-3501 Parallel Programming | (部分) Lock Manager |
| **Cambridge** | Part II: Concurrent Systems | MVCC, 2PL |

## 构建与测试

```bash
make          # 编译所有模块和 demo
make test     # 运行全部测试 (34/34 通过)
make clean    # 清理构建产物
```

运行 demo：
```bash
./bin/buffer_pool_demo   # 缓冲区池 LRU 演示
./bin/btree_demo          # B+Tree 插入/删除/范围扫描演示
./bin/wal_demo            # WAL 恢复演示
```

## 目录结构

```
mini-db-kernel/
├── Makefile
├── README.md
├── include/          (10 header files)
├── src/              (10 implementation files)
├── tests/             (5 test suites, 34 test cases)
├── examples/          (3 demo programs)
├── demos/             (mini-storage-engine + mini-transactions)
├── docs/              (course-alignment + kernel primer)
└── bin/               (build output)
```

## 跨模块集成数据流

```
Write: Client → KVS API → B+Tree → Buffer Pool → Slotted Page → WAL → Disk Manager
Read:  Client → KVS API → B+Tree → Buffer Pool → Slotted Page → Disk Manager
Txn:   Client → KVS API → MVCC (Snapshot) + Lock Manager (2PL) → WAL
Recovery: Crash → WAL → ARIES (Analysis→Redo→Undo) → Consistent State
```

## 设计原则

- **C99**: 标准 C99 (`stdint.h`, `stdbool.h`)
- **最小依赖**: 仅 libc + libm
- **教学导向**: 优先代码可读性和知识覆盖
- **每个函数实现一个独立知识点**: 禁止凑行数

## 许可证

本项目仅用于教学目的。
