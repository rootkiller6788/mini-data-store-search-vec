# mini-file-storage — 文件存储引擎 (C 语言实现)

> 参考 LevelDB, RocksDB, ScyllaDB, Bitcask

## Module Status: COMPLETE ✅

- **L1-L6**: Complete (all core definitions, concepts, structures, theorems, algorithms, and canonical problems implemented)
- **L7**: Complete (4 applications: KV store, WAL recovery, range queries, composite key encoding)
- **L8**: Complete (2 advanced topics: MVCC snapshots, LRU block cache with hit rate analysis)
- **L9**: Partial (documented, not implemented — industrial compaction optimizations)

---

## 概述

`mini-file-storage` 是一个用 C99 编写的 **LSM-Tree 存储引擎** 教育实现。涵盖现代存储引擎的核心组件：Skip List Memtable、SSTable 文件、WAL 崩溃恢复、Bloom Filter、Compaction 策略、Block Cache、Range Query、MVCC Snapshot 和 Key Encoding。

## 项目结构

```
mini-file-storage/
├── include/
│   ├── sstable.h              SSTable 文件格式 (数据块/索引/Footer/Bloom)
│   ├── lsm_tree.h             LSM-Tree 引擎 (Memtable/Immutable/Levels)
│   ├── skiplist.h             跳表 (Memtable 数据结构)
│   ├── wal_file.h             WAL 预写日志 (崩溃恢复)
│   ├── compaction.h           Compaction 合并策略
│   ├── block_cache.h          LRU 块缓存 (读写优化)
│   ├── range_query.h          Key Range 扫描
│   ├── snapshot.h             MVCC 快照隔离
│   └── encoding.h             Varint/Big-Endian/Composite Key 编码
├── src/
│   ├── sstable.c              SSTable 序列化/反序列化/查找/Bloom
│   ├── lsm_tree.c             LSM-Tree put/get/compact/close
│   ├── skiplist.c             跳表 创建/插入/查找/迭代/销毁
│   ├── wal_file.c             WAL 打开/追加/同步/恢复
│   ├── compaction.c           多路归并/Leveled/Tiered 策略
│   ├── block_cache.c          O(1) LRU 驱逐 (Hash Table + Doubly-Linked List)
│   ├── range_query.c          多维归并范围扫描 + 流式迭代器
│   ├── snapshot.c             Sequence-based MVCC + Version GC
│   └── encoding.c             Varint/Composite Key/Common Prefix
├── tests/
│   └── test_suite.c           27 项综合测试 (skip list, WAL, SSTable, LSM, cache, range, MVCC, encoding)
├── examples/
│   ├── sstable_demo.c         SSTable 100 records + Bloom FPR 测试
│   ├── lsm_demo.c             LSMTree 1000 keys + compaction + get
│   └── skiplist_demo.c        SkipList 50 keys + 搜索 + 迭代
├── demos/
│   ├── mini-leveldb-engine/README.md       LevelDB 内部架构深度解析
│   └── mini-compaction-strategies/README.md Compaction 策略完整对比
├── docs/
│   ├── storage-engine-architecture.md      整体架构文档
│   └── course-alignment.md                课程对齐
├── Makefile
└── README.md
```

## 快速开始

### 构建

```bash
make all          # 构建库 + 所有示例
make test         # 构建并运行 27 项测试
make clean        # 清理构建产物
```

### 运行示例

```bash
# SSTable 读写 + Bloom Filter 测试
./build/sstable_demo

# 跳表插入/查找/迭代性能
./build/skiplist_demo

# LSM-Tree 完整流程 (put → compact → get)
./build/lsm_demo
```

### 测试覆盖

| 模块 | 测试数 | 测试内容 |
|------|--------|---------|
| SkipList | 5 | 创建、插入/搜索、500条随机、迭代器有序性、重复key更新 |
| WAL | 2 | 50条追加/恢复、CRC32C完整性 |
| SSTable | 2 | 100条写入/读回/Bloom验证、3路归并迭代器 |
| LSM Tree | 2 | 200条put/get、delete验证 |
| Block Cache | 3 | 10条put/get/命中率、LRU驱逐、invalidate |
| Range Query | 3 | 单SSTable范围扫描、key_compare语义、边界条件 |
| Snapshot/MVCC | 4 | Snapshot管理器、版本存储、Tombstone、GC |
| Encoding | 5 | Varint32/64、Big-Endian、Composite Key、Prefix |
| Compaction | 1 | Leveled策略选择 |

## 九层知识覆盖

### L1 — 核心定义 (Complete)

| 结构 | 文件 | 知识点 |
|------|------|--------|
| `SkipList` / `SkipNode` | `skiplist.h` | 概率跳表数据结构 |
| `SSTable` / `SSTableDataBlock` / `SSTableIndexBlock` / `SSTableFooter` | `sstable.h` | 磁盘文件格式 |
| `BloomFilter` | `sstable.h` | 概率数据结构 |
| `LSMTree` / `LSLevel` / `ImmutableMem` | `lsm_tree.h` | LSM树引擎结构 |
| `WALWriter` / `WALRecord` | `wal_file.h` | 预写日志 |
| `CompactionJob` / `CompactionStats` | `compaction.h` | Compaction抽象 |
| `BlockCache` / `BCacheEntry` | `block_cache.h` | LRU缓存 |
| `KeyRange` / `RangeEntry` / `RangeIterator` | `range_query.h` | 范围查询 |
| `Snapshot` / `SnapshotManager` / `VersionStore` | `snapshot.h` | MVCC快照 |
| `CompositeKeyBuilder` | `encoding.h` | 复合键编码 |

### L2 — 核心概念 (Complete)

| 概念 | 实现 | 理论来源 |
|------|------|---------|
| LSM-Tree | `lsm_tree.c` | O'Neil et al., Acta Informatica 1996 |
| Write-Ahead Logging | `wal_file.c` | Mohan et al., ARIES 1992 |
| Bloom Filter | `sstable.c` | Bloom, CACM 1970 |
| Skip List | `skiplist.c` | Pugh, CACM 1990 |
| SSTable / Sorted String Table | `sstable.c` | BigTable, Google 2006 |
| Leveled Compaction | `compaction.c` | LevelDB, Google 2011 |
| LRU Cache | `block_cache.c` | Belady, IBM Systems Journal 1966 |
| Range Scan | `range_query.c` | Luo et al., VLDB Journal 2020 |
| MVCC Snapshot Isolation | `snapshot.c` | Bernstein & Goodman, ACM TODS 1983 |
| Delta Encoding | `sstable.c` / `encoding.c` | Garcia-Molina et al., Database Systems |

### L3 — 工程结构 (Complete)

| 结构 | 描述 |
|------|------|
| LSM Multi-Level Hierarchy | 7层Level，Level 0可重叠，Level 1+不重叠 |
| Immutable Memtable Queue | 冻结队列 FIFO flush |
| SSTable On-Disk Layout | Data Block → Index Block → Bloom Filter → Footer |
| Restart Point Index | 每16 entry一个restart point，支持块内二分查找 |
| K-Way Merge Heap | Min-Heap用于多路归并Compaction |
| Hash Table + Doubly-Linked List | LRU Cache O(1)访问 |
| Version Chain | 每个Key维护从新到旧的VersionNode链表 |
| Composite Key Separator | 0x00字节分隔，内部0x00/0xFF转义 |

### L4 — 标准/定理 (Complete)

| 定理 | 验证位置 | 公式 |
|------|---------|------|
| Bloom Filter FPR | `sstable.c` | P ≈ (1 - e^(-kn/m))^k, k=3, m/n=10 → P≈0.82% |
| LRU Competitive Ratio | `block_cache.c` | k-competitive (Sleator & Tarjan, 1985) |
| MVCC SI Correctness | `snapshot.c` | Read at S sees all writes with seqno ≤ S |
| GC Safety | `snapshot.c` | Version V is removable iff V < min(active snapshots) |
| Varint Completeness | `encoding.c` | decode(encode(v)) = v for all v ∈ [0, 2^32-1] |
| LSM Write Amplification | `demos/mini-compaction-strategies/` | WA = (L+1) · S_{L+1} / S_0 for leveled |
| CRC32C Guarantee | `wal_file.c` | Detects all single-bit and burst errors ≤ 32 bits |

### L5 — 算法/方法 (Complete)

| 算法 | 实现 | 复杂度 |
|------|------|--------|
| Skip List Search/Insert | `skiplist.c` | O(log n) expected |
| K-Way Merge (Min-Heap) | `compaction.c` | O(N log K) |
| Restart-Point Binary Search | `sstable.c` | O(log R + M), R=restarts, M=entries in group |
| Prefix Delta Encoding | `sstable.c` | O(common_prefix) |
| LRU Eviction | `block_cache.c` | O(1) per operation |
| Varint LEB128 Encoding | `encoding.c` | O(1) per integer, 1-5 bytes for uint32 |
| Composite Key Build with Escape | `encoding.c` | O(n) per component |
| MVCC Version Chain Walk | `snapshot.c` | O(v), v = number of versions |
| Garbage Collection | `snapshot.c` | O(n·v_bar), n = keys, v_bar = avg versions |
| Range Scan with Merge Dedup | `range_query.c` | O(R · K), R = rows, K = overlapping tables |

### L6 — 经典工程问题 (Complete)

| 问题 | 示例/测试 | 验证 |
|------|----------|------|
| KV Store | `examples/lsm_demo.c` | 1000 key put/get, compaction, random reads |
| WAL Crash Recovery | `tests/test_suite.c` | 50 records append/recover, CRC32C integrity |
| SSTable Builder/Reader | `examples/sstable_demo.c` | 100 records write/read-back/verify |
| Merge Sort on SSTables | `tests/test_suite.c` | 3-way merge iterator ordering |
| LRU Block Cache | `tests/test_suite.c` | Hit rate, eviction, invalidation |
| Range Query | `tests/test_suite.c` | [010,030) range scan returns exactly 20 |
| MVCC Snapshot Read | `tests/test_suite.c` | Version at seqno 1 vs seqno 5 correctness |

### L7 — 应用 (Complete, 4个)

| 应用 | 描述 |
|------|------|
| 1. Point Lookup KV Store | `lsm_tree.c` — LSM-based read/write path |
| 2. Crash Recovery with WAL | `wal_file.c` — CRC32C-verified replay |
| 3. Range Scan over SSTables | `range_query.c` — Key range with merge & filter |
| 4. Composite Key Encoding | `encoding.c` — Tuple encoding for secondary indexes |

### L8 — 进阶主题 (Complete, 2个)

| 主题 | 实现 |
|------|------|
| MVCC Snapshot Isolation | `snapshot.c` — Sequence-number-based read views, version GC |
| LRU Block Cache | `block_cache.c` — O(1) eviction, hit rate analytics |

### L9 — 工业前沿 (Partial, 仅文档)

| 主题 | 参考 |
|------|------|
| Tiered/Universal Compaction | `demos/mini-compaction-strategies/README.md` |
| LevelDB Write Path Optimization | `demos/mini-leveldb-engine/README.md` |
| AI-Driven Compaction Scheduling | 未来方向 (Auto-tuning) |

## 九校课程映射

| 学校 | 关键课程 | 模块对应 |
|------|---------|---------|
| **MIT** | 6.824 Distributed Systems | WAL, LSM-Tree (BigTable/Cassandra基础) |
| **Stanford** | CS 245 Database | Bloom Filter, Range Query, MVCC |
| **CMU** | 15-445 Database Systems | SSTable format, Compaction strategies |
| **Berkeley** | CS 186 Database | LSM-Tree, Write amplification analysis |
| **UT Austin** | CS 380D Distributed | WAL crash recovery, CRC integrity |
| **ETH** | 263-3501 Parallel Programming | K-way merge, Min-Heap |
| **Cambridge** | Part II: Concurrent Systems | Lock-free Skip List (foundation) |
| **清华** | 操作系统 | File I/O, buffered writes, fsync |
| **Georgia Tech** | CS 6210 Advanced OS | LRU page replacement, cache theory |

## 核心定理 (含公式)

1. **Bloom Filter False Positive Rate**
   ```
   P(false positive) = (1 - e^{-kn/m})^k
   where k = 3 hash functions, m/n = 10 bits per key
   P ≈ 0.0082 (0.82%)
   ```

2. **LRU Competitive Ratio**
   ```
   CR(LRU) = k (cache size)
   Optimal offline (Belady's MIN) ≤ k · LRU
   ```

3. **LSM Write Amplification (Leveled)**
   ```
   WA = (L+1) · S_{L+1} / S_0
   where L = number of levels, S_i = size of level i
   ```

4. **MVCC Visibility**
   ```
   ∀ snapshot S, ∀ key K: visible(S, K) = max{seqno(v) : seqno(v) ≤ S}
   ```

## 技术约束

- C99 标准
- 仅依赖 libc + libm
- 单线程（无并发控制）
- Key ≤ 256 bytes, Value ≤ 1024 bytes
- 教育目的：代码清晰优先于性能

## 参考资料

- [LevelDB](https://github.com/google/leveldb)
- [RocksDB](https://github.com/facebook/rocksdb)
- [ScyllaDB Architecture](https://docs.scylladb.com/stable/architecture/)
- [Bitcask Paper](https://riak.com/assets/bitcask-intro.pdf)
- [BigTable (Google, 2006)](https://research.google/pubs/pub27898/)
- [LSM-Tree Paper (1996)](https://www.cs.umb.edu/~poneil/lsmtree.pdf)
- [Bloom, "Space/Time Trade-offs in Hash Coding", CACM 1970](https://doi.org/10.1145/362686.362692)
- [Bernstein & Goodman, "Multiversion Concurrency Control", ACM TODS 1983](https://doi.org/10.1145/319996.319998)

## License

MIT
