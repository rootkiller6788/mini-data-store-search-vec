# 数据库内核入门 (Database Kernel Primer)

> 数据库管理系统（DBMS）内核的核心概念与组件概述

## 什么是数据库内核？

数据库内核（Database Kernel）是数据库管理系统的核心引擎，负责数据存储、检索、并发控制和事务管理。它位于 SQL 解析器和优化器之下，磁盘文件系统之上，是 DBMS 性能和数据一致性的关键。

```
+------------------------------------------+
|            Client / Application           |
+------------------------------------------+
                    | SQL
+------------------------------------------+
|   SQL Parser → Binder → Optimizer →      |
|   Query Executor (Volcano Model)         |
+------------------------------------------+
                    | Plan
+------------------------------------------+
|          DATABASE KERNEL                 |
|  +-----------+  +-----------+           |
|  | Storage   |  | Indexing  |           |
|  | Engine    |  | (B+Tree)  |           |
|  +-----------+  +-----------+           |
|  +-----------+  +-----------+           |
|  | Concurrency| | Recovery  |           |
|  | (2PL/MVCC)| | (WAL/ARIES)|           |
|  +-----------+  +-----------+           |
+------------------------------------------+
                    | Pages
+------------------------------------------+
|    Disk Manager / File System            |
+------------------------------------------+
```

## 核心组件

### 1. 存储引擎 (Storage Engine)

存储引擎负责数据的物理组织方式：

**页面（Page）：** 数据库 I/O 的最小单位，通常 4KB 或 8KB。所有磁盘读写都以页面为单位。

**页面布局：**
```
+-------------------+
| Page Header       | ← page_id, LSN, slot count, free space ptr
+-------------------+
| Slot Array        | ← (offset, length) pairs
+-------------------+
| Free Space        |
+-------------------+
| Tuple Data        | ← actual records (inserted from end)
+-------------------+
```

**存储模型：**
- **N-ary Storage Model (NSM)**：行存，适合 OLTP（事务型）
- **Decomposition Storage Model (DSM)**：列存，适合 OLAP（分析型）

**文件组织：**
- **Heap File**：无序存储，适合频繁插入
- **Sorted File**：按主键排序，适合范围查询
- **Hash File**：哈希组织，适合等值查询
- **Tree File (B+Tree)**：树状组织，兼顾点查和范围查询

### 2. 缓冲区池 (Buffer Pool)

**问题：** 磁盘 I/O 比内存访问慢 100,000 倍以上。如何减少磁盘 I/O？

**解决方案：** Buffer Pool 缓存最近使用的数据页。

```
                    Buffer Pool
                +-----+-----+-----+
Disk ←→ Cache   | F1  | F2  | ... | F1024 |
                +-----+-----+-----+
                   ↑
                   | (page_id → frame_idx via hash table)
                   |
               Page Table

Replacement Policy:
  - LRU (Least Recently Used): evict oldest unpinned page
  - Clock: approximate LRU with reference bit
  - LRU-K: consider last K references

Pin Count: prevents eviction of actively-used pages
Dirty Flag: must write dirty pages back to disk on eviction
```

**关键指标：**
- **Hit Ratio** = hits / (hits + misses)
- 目标：> 95% 命中率（取决于工作负载和池大小）

### 3. B+Tree 索引

B+Tree 是数据库中最广泛使用的索引结构：

**性质：**
1. 所有键按序存储
2. 所有数据存储在叶子节点
3. 内部节点只存储路由此（separator keys）
4. 叶子节点通过链表连接，支持高效范围扫描
5. 每个节点有填充度约束：[⌈order/2⌉-1, order-1] 个键

**操作复杂度：**
| 操作 | 复杂度 |
|:---|:---|
| Search | O(log_B N) |
| Insert | O(log_B N) |
| Delete | O(log_B N) |
| Range Scan | O(log_B N + K) |

**为什么用 B+Tree 而不是 B-Tree？**
- B+Tree 的内部节点不放数据 → 更高扇出 → 更矮的树 → 更少 I/O
- B+Tree 的叶子链表 → 高效范围扫描

**分裂与合并：**
```
Before Split:           After Split (put 15):
  [5, 10, 20, 25]        [5, 10]   [20, 25]
                              \      /
                         [15] → intermediate key
                              /      \
                         Internal node (parent)
```

### 4. 事务与 ACID

**事务（Transaction）：** 一组操作的集合，要么全部执行，要么全部不执行。

**ACID 属性：**

| 属性 | 含义 | 实现机制 |
|:---|:---|:---|
| **A**tomicity | 全有或全无 | WAL + Undo Logging |
| **C**onsistency | 数据完整性 | 约束检查 + 索引维护 |
| **I**solation | 事务间不干扰 | 2PL, MVCC, Snapshot Isolation |
| **D**urability | 提交后不丢失 | WAL + Checkpoint |

### 5. 并发控制

**问题：** 多个事务同时访问相同数据，如何避免冲突？

**异常现象：**
- **Dirty Read**：读到未提交的修改
- **Lost Update**：覆盖另一个事务的写入
- **Non-Repeatable Read**：同一事务内两次读到不同版本
- **Phantom Read**：同一条件查询在不同时间返回不同行

**隔离级别：**
| Level | Dirty Read | Lost Update | Non-Repeatable Read | Phantom |
|:---|:---|:---|:---|:---|
| Read Uncommitted | Yes | Yes | Yes | Yes |
| Read Committed | No | Yes | Yes | Yes |
| Repeatable Read | No | No | No | Yes |
| Serializable | No | No | No | No |

**2PL (Two-Phase Locking)：**
- Phase 1 (Growing)：只获取锁
- Phase 2 (Shrinking)：只释放锁
- 保证冲突可串行化
- 问题：可能出现死锁（需要检测或预防）

**MVCC (Multi-Version Concurrency Control)：**
- 写操作不覆盖旧版本，而是创建新版本
- 读操作看到事务开始时的快照
- 读不会被写阻塞（反之亦然）

```
Version Chain for tuple X:
  [v3: txn=5 created, active] → [v2: txn=3 committed] → [v1: txn=1 committed]

Txn 7 snapshot: active={5} → sees v3? No (active) → sees v2 (committed)
Txn 8 snapshot: active={5} → sees v3? No (active) → sees v2 (committed)
Txn 5 reads own write → sees v3 (self-created)
```

### 6. 恢复 (Recovery)

**WAL (Write-Ahead Logging)：**
规则：在数据页刷到磁盘之前，其对应的 WAL 记录必须先刷到磁盘。

```
WAL Protocol:
  1. Write WAL record
  2. Force WAL to disk (fsync)
  3. Apply change to data page (in buffer pool)
  4. Data page can be flushed later (No-Force, Steal policy)
```

**ARIES Recovery Algorithm：**
1. **Analysis Phase**：前向扫描 WAL，构建脏页表和事务表
   - 确定哪些事务已提交（有 COMMIT）、哪些未提交
   - 确定 checkpoint 以来的所有脏页
2. **Redo Phase**：从最早脏页的 LSN 开始前向应用所有操作
   - 策略：Repeating History（重复历史）
   - 即使事务最终失败也 Redo（简化 Undo）
3. **Undo Phase**：后向扫描，撤销未提交事务的操作
   - 对每个未提交事务，从最后一条记录开始 Undo
   - 每次 Undo 写 CLR (Compensation Log Record)

## 数据库内核的关键设计决策

| 决策 | 选项 | 权衡 |
|:---|:---|:---|
| 存储模型 | 行存 vs 列存 | OLTP 优化 vs OLAP 优化 |
| 页面大小 | 4KB vs 8KB vs 16KB | 更细粒度 vs 更少 I/O 次数 |
| 替换策略 | LRU vs Clock vs LRU-K | 精确度 vs 实现复杂度 |
| 索引结构 | B+Tree vs LSM Tree | 读优化 vs 写优化 |
| 并发控制 | 2PL vs MVCC vs OCC | 冲突多 vs 多版本开销 vs 乐观 |
| 恢复策略 | ARIES vs Shadow Paging | 工业标准 vs 简洁但浪费空间 |

## 参考资料

- **CMU 15-445/645 Database Systems** — Andy Pavlo, CMU
- **Database Internals** — Alex Petrov, O'Reilly (2020)
- **Database System Concepts** — Silberschatz, Korth, Sudarshan
- **Architecture of a Database System** — Hellerstein, Stonebraker, Hamilton (2007)
- **ARIES: A Transaction Recovery Method** — C. Mohan et al., SIGMOD 1992
- **A Critique of ANSI SQL Isolation Levels** — Berenson et al., SIGMOD 1995
