# mini-storage-engine — 微型存储引擎

> 基于 Buffer Pool + B+Tree + WAL 的简易存储引擎实现

## 概述

mini-storage-engine 是一个用 C99 实现的微型磁盘存储引擎，演示了数据库存储层的三个核心组件如何协同工作：
- **Buffer Pool**：内存页面缓存，减少磁盘 I/O
- **B+Tree**：有序索引结构，支持点查和范围扫描
- **WAL**：Write-Ahead Log，保证持久性和崩溃恢复

## 架构

```
+--------------------------------------------------+
|                   Storage Engine                  |
|                                                   |
|  +-----------+  +-----------+  +-----------+      |
|  | Buffer    |  | B+Tree    |  | WAL       |      |
|  | Pool      |  | Index     |  | Manager   |      |
|  +-----------+  +-----------+  +-----------+      |
|        |              |               |            |
|        v              v               v            |
|  +-------------------------------------------+    |
|  |              Page Manager                  |    |
|  +-------------------------------------------+    |
|        |                                          |
|        v                                          |
|  +-------------------------------------------+    |
|  |           Disk (Simulated)                 |    |
|  +-------------------------------------------+    |
+--------------------------------------------------+
```

## 组件详解

### 1. Buffer Pool (缓冲区池)

缓冲区池是数据库内存管理的核心。它将磁盘上的页面（4KB）缓存到内存中，减少实际的磁盘读写操作。

**核心结构：**
- `Frame`：每个帧包含一个 4KB 的数据页、页面 ID、pin 计数和 dirty 标志
- `BufferPool`：管理 1024 个帧的池，使用哈希表做 page_id → frame_idx 映射
- LRU 链表：最近最少使用的淘汰策略

**关键操作：**
- `bp_fetch_page(page_id)`：获取页面，如果已在池中则命中（hits++），否则加载或淘汰旧页（misses++）
- `bp_unpin_page(page_id)`：释放页面引用（pin_count--）
- `bp_flush_page(page_id)`：将 dirty 页面写回磁盘
- `bp_flush_all()`：刷新所有 dirty 页面

**LRU 淘汰策略：**
当缓冲池满时，从 LRU 链表尾部选择 pin_count == 0 的页面淘汰。如果被淘汰的页面是 dirty 的，先写回磁盘。只淘汰 unpinned 的页面，pin 住的页面不会被淘汰。

**哈希页表：**
使用乘法哈希（Knuth's golden ratio）做 page_id → frame_idx 的快速映射，链地址法解决冲突。

### 2. B+Tree (B+树索引)

B+树是数据库中最常用的索引结构。所有数据存储在叶子节点，内部节点只存路由键，叶子节点通过链表连接支持高效范围扫描。

**核心结构：**
- `BTreeNode`：包含 keys[]、children[]（内部节点）/ values[]（叶子节点）、is_leaf 标志、next_leaf 指针
- Order = 5：每个节点最多 4 个键，最少 2 个键（除根节点外）

**关键操作：**
- `btree_insert(key, value)`：插入键值对，遇到满节点时分裂
- `btree_search(key, *value)`：从根到叶子的二分查找
- `btree_search_range(low, high, ...)`：利用叶子链表做范围扫描
- `btree_delete(key)`：删除键，处理下溢（从兄弟借或与兄弟合并）

**分裂策略（插入时）：**
1. 如果根节点满，创建新根并将原根分裂
2. 中间键提升到父节点
3. 叶子节点更新 next_leaf 指针维护链表

**合并策略（删除时）：**
1. 优先从左兄弟借键
2. 其次从右兄弟借键
3. 都不够则与兄弟合并，父节点键下移

**范围扫描：**
从包含 low 的叶子节点开始，沿 next_leaf 指针遍历，直到键超过 high。复杂度 O(log N + K)，K 为结果数。

### 3. WAL (Write-Ahead Log)

WAL 是保证事务持久性的关键机制。所有修改先写入 WAL 日志（刷到磁盘），然后才应用到实际数据页。崩溃后通过重放 WAL 恢复。

**核心结构：**
- `WALRecord`：包含 LSN（日志序列号）、类型、页面 ID、事务 ID、before/after 数据
- `WALManager`：管理 WAL 记录数组、flush_lsn、checkpoint_lsn

**记录类型：**
- `WAL_INSERT`：插入操作
- `WAL_UPDATE`：更新操作（包含 before 和 after 镜像）
- `WAL_DELETE`：删除操作（只有 before 镜像）
- `WAL_COMMIT`：事务提交
- `WAL_ABORT`：事务中止
- `WAL_BEGIN_CHECKPOINT`：检查点开始

**ARIES 恢复协议（简化版）：**
1. **Analysis 阶段**：扫描 WAL，识别已提交和未提交的事务
2. **Redo 阶段**：重做已提交事务的所有操作（重复历史）
3. **Undo 阶段**：反序撤销未提交事务的操作（回滚）

**WAL 规则：**
- 事务提交记录必须在所有修改记录之后写入
- 在数据页刷到磁盘之前，必须先刷对应的 WAL 记录
- Checkpoint 记录当前 flushed LSN，加速恢复

## 数据流

```
写入路径:  Client → B+Tree → Buffer Pool → WAL → "Disk"
           (实际实现中 WAL 先写，Buffer Pool 后写)

读取路径:  Client → B+Tree → Buffer Pool → Frame Data
           (如果未命中则从"磁盘"加载)

恢复路径:  WAL → Analysis → Redo → Undo → Buffer Pool
```

## 编译与运行

```bash
cd mini-db-kernel
make

# 缓冲区池演示
./bin/buffer_pool_demo

# B+树演示
./bin/btree_demo

# WAL 恢复演示
./bin/wal_demo
```

## 局限性（本教学实现）

1. **单线程**：不涉及并发控制
2. **内存磁盘**：磁盘 I/O 通过内存模拟，未使用实际文件
3. **简化 B+Tree**：children 存储为 page_id 指针，非磁盘页面 ID
4. **无日志持久化**：WAL 仅存储在内存中
5. **无页目录**：缺少真实的页面文件组织

## 与 CMU 15-445 的关系

本实现对应于 CMU 15-445 课程中的以下项目：
- **Project 1**：Buffer Pool Manager — 本项目的 `buffer_pool.h/c`
- **Project 2**：B+Tree Index — 本项目的 `btree.h/c`
- **Project 3**（部分）：Log Manager — 本项目的 `wal.h/c`

## 参考资料

- CMU 15-445/645: Database Systems, Andy Pavlo
- Database Internals (Alex Petrov), Chapter 2 (B-Trees), Chapter 3 (Buffer Pool)
- ARIES: A Transaction Recovery Method Supporting Fine-Granularity Locking, Mohan et al.
