# mini-file-storage — 文件存储引擎 (C 语言实现)

> 参考 LevelDB, RocksDB, ScyllaDB, Bitcask

---

## 概述

`mini-file-storage` 是一个用 C99 编写的 **LSM-Tree 存储引擎** 教育实现。它涵盖了现代存储引擎的核心组件：Skip List Memtable、SSTable 文件、WAL 崩溃恢复、Bloom Filter 和 Compaction 策略。

## 项目结构

```
mini-file-storage/
├── include/
│   ├── sstable.h              SSTable 文件格式 (数据块/索引/Footer/Bloom)
│   ├── lsm_tree.h             LSM-Tree 引擎 (Memtable/Immutable/Levels)
│   ├── skiplist.h             跳表 (Memtable 数据结构)
│   ├── wal_file.h             WAL 预写日志 (崩溃恢复)
│   └── compaction.h           Compaction 合并策略
├── src/
│   ├── sstable.c              SSTable 序列化/反序列化/查找/Bloom
│   ├── lsm_tree.c             LSM-Tree put/get/compact/close
│   ├── skiplist.c             跳表 创建/插入/查找/迭代/销毁
│   ├── wal_file.c             WAL 打开/追加/同步/恢复
│   └── compaction.c           多路归并/Leveled/Tiered 策略
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

| 示例 | 测试内容 |
|------|---------|
| `sstable_demo` | 100 条记录写入、读回、Bloom Filter 误判率 |
| `skiplist_demo` | 50 随机 key 插入、有序遍历、搜索、10000 次查找 |
| `lsm_demo` | 1000 key put → compaction → get → 5000 随机读压测 |

## 核心组件

### SkipList (Memtable)
- 最大 12 层，p=0.5 晋升概率
- O(log n) 查找/插入
- 有序迭代器支持

### SSTable
- Data Block + Index Block + Bloom Filter + Footer
- 前缀压缩 (共享前缀 Delta 编码)
- 16 entry 间隔的 Restart Points (支持二分查找)

### WAL
- CRC32C 校验和
- PUT/DELETE 记录
- 崩溃恢复重放

### Compaction
- **Leveled**: 低读放大，适合读密集型
- **Tiered/Universal**: 低写放大，适合写密集型
- 多路归并 (Min-Heap K-way merge)
- Compaction Stats 输出

### Bloom Filter
- 3 哈希函数，10 bits/key
- 误判率约 0.82%
- 快速过滤不存在的 key

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

## License

MIT
