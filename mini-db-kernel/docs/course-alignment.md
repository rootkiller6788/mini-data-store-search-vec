# CMU 15-445 Course Alignment

> 本项目的各组件与 CMU 15-445/645 Database Systems 课程内容的映射关系

## 课程架构总览

CMU 15-445 分为以下几个主要模块：

```
Relational Model → SQL → Storage → Indexing → Query Execution
                                        ↓
                              Concurrency Control → Recovery
```

本项目聚焦在三个核心模块：**Storage**、**Indexing**、**Concurrency & Recovery**。

---

## 1. Buffer Pool — Project 1: Buffer Pool Manager

**课程对应：** Lecture 04-05 (Database Storage)

| CMU 15-445 概念 | 本项目实现 | 说明 |
|:---|:---|:---|
| Page / Frame | `Frame` struct (page_id, pin_count, dirty, data[4096]) | 4096-byte pages |
| Buffer Pool Manager | `BufferPool` struct + `bp_*` functions | Capacity 1024 frames |
| Page Table (page_id → frame) | Hash table with chaining | O(1) average lookup |
| Replacement Policy (LRU) | LRU linked list | Exact LRU, not clock |
| Pin / Unpin | pin_count field | Prevents eviction of in-use pages |
| Dirty flag + flush | dirty bool, bp_flush_page / bp_flush_all | Write-back on eviction |

**课程概念映射：**
- **Frame** — Lecture 04: Slotted Pages, Page layout
- **Buffer Pool** — Lecture 05: Buffer pool organization
- **Page Table** — Project 1: PageTable mapping
- **LRU Replacement** — Lecture 05: Replacement policies (LRU, Clock, LRU-K)
- **Pin/Unpin** — Project 1: FetchPage / UnpinPage interface

---

## 2. B+Tree — Project 2: B+Tree Index

**课程对应：** Lecture 07-08 (Tree Indexes)

| CMU 15-445 概念 | 本项目实现 | 说明 |
|:---|:---|:---|
| B+Tree Structure | `BTreeNode` struct | Order 5, max 4 keys per node |
| Internal Nodes | keys[] + children[] | Separator keys + pointers |
| Leaf Nodes | keys[] + values[] + next_leaf | Pointers to actual data + chain |
| Insert | btree_insert() with split | Full node → split, promote middle key |
| Delete | btree_delete() with rebalance | Underflow → borrow or merge |
| Point Query | btree_search() | Binary search in node, traverse to leaf |
| Range Scan | btree_search_range() | Leaf chain traversal |
| Sibling pointers | next_leaf | Ordered leaf scan without tree traversal |

**课程概念映射：**
- **B+Tree vs B-Tree** — Lecture 07: B+Tree data in leaves only
- **Insert Algorithm** — Lecture 07: Split, promote, create new root if needed
- **Delete Algorithm** — Lecture 07: Redistribute (borrow) or coalesce (merge)
- **Leaf Node Scans** — Project 2: Scan iterator using sibling pointers
- **Fanout / Order** — Lecture 07: Node capacity and tree height

---

## 3. WAL & ARIES Recovery — Lecture 18-19

**课程对应：** Lecture 18-19 (Database Logging & Recovery)

| CMU 15-445 概念 | 本项目实现 | 说明 |
|:---|:---|:---|
| WAL Records | `WALRecord` struct | LSN, type, page_id, before/after data |
| Log Sequence Number (LSN) | lsn field (monotonic) | Monotonically increasing |
| Write-Ahead Logging | wal_write() before data modification | Log before page |
| Flush | wal_flush() | Force log to stable storage |
| ARIES Recovery | wal_recover() | Analysis → Redo → Undo |
| Checkpoint | wal_checkpoint() | Record flushed LSN, reduce recovery time |
| Compensation Log Records | (simplified) | Implicit in before/after data |
| UNDO logging | before_data[] in records | Physical undo (full page before-image) |

**课程概念映射：**
- **WAL Protocol** — Lecture 18: Log records must be on disk before data pages
- **ARIES Main Principles** — Lecture 18: WAL, Repeating History, Undoing Changes
- **ARIES Recovery Phases** — Lecture 19: Analysis → Redo → Undo
- **Checkpointing** — Lecture 19: Reduce recovery time, fuzzy vs sharp
- **Transaction Rollback** — Lecture 18: Using UNDO log records

---

## 4. Lock Manager (2PL) — Lecture 14-15

**课程对应：** Lecture 14-15 (Concurrency Control Theory + 2PL)

| CMU 15-445 概念 | 本项目实现 | 说明 |
|:---|:---|:---|
| Lock Modes | `LockMode` enum (SHARED, EXCLUSIVE) | S (read), X (write) |
| Compatibility Matrix | lm_lock_acquire() conflict check | S+S=ok, S+X=no, X+X=no |
| Two-Phase Locking | (interface enforces 2PL convention) | Growing then shrinking |
| Lock Table | Hash buckets (256 buckets) | Resource → LockEntry chain |
| Wait Queue | queue_next/queue_prev | Sleep until lock available |
| Deadlock Detection | lm_detect_deadlock() | Waits-for graph cycle detection |
| Grant/Wait | LockEntry.granted field | Mark whether lock is held or waiting |

**课程概念映射：**
- **Lock Types** — Lecture 14: Shared vs Exclusive, Intention Locks
- **2PL Phases** — Lecture 14: Growing (acquire only) → Shrinking (release only)
- **Deadlock** — Lecture 15: Waits-for graph, detection vs prevention
- **Lock Manager** — Project 3: LockManager with lock/unlock interface
- **Hierarchical Locking** — Lecture 14 (not implemented here): IS, IX, SIX modes

---

## 5. MVCC — Lecture 16-17

**课程对应：** Lecture 16-17 (Timestamp Ordering + MVCC)

| CMU 15-445 概念 | 本项目实现 | 说明 |
|:---|:---|:---|
| Tuple Versioning | `TupleVersion` struct | txn_id_begin, txn_id_end, data chain |
| Version Chain | next_version pointer | Linked list of versions |
| Snapshot Isolation | `MVCCTransaction` snapshot | xmin, xmax, active_txns[] |
| Visibility Rules | mvcc_read() visibility check | begin ≤ snapshot, end > snapshot, not active |
| Garbage Collection | mvcc_gc() | Vacuum old versions |
| Write Skew | (not handled) | Known limitation of snapshot isolation |

**课程概念映射：**
- **MVCC Overview** — Lecture 16: Multiple versions per tuple, no overwrites
- **Snapshot Isolation** — Lecture 16: Each txn sees snapshot of committed data
- **Version Visibility** — Lecture 16: Tuple visibility at transaction level
- **Version Storage** — Lecture 16: Append-only storage, version chain
- **Garbage Collection** — Lecture 17: Tuple-level vs transaction-level GC
- **Write-Write Conflicts** — Lecture 16: First-committer-wins rule

---

## 项目体系映射

| CMU 15-445 Project | 本项目文件 | 权重 |
|:---|:---|:---|
| Project 1: Buffer Pool | `include/buffer_pool.h`, `src/buffer_pool.c` | 25% |
| Project 2: B+Tree Index | `include/btree.h`, `src/btree.c` | 25% |
| Project 3: Lock Manager | `include/lock_manager.h`, `src/lock_manager.c` | 15% |
| Project 3: Transaction Manager | `include/mvcc.h`, `src/mvcc.c` | 15% |
| Project 4: Log Manager | `include/wal.h`, `src/wal.c` | 20% |

## 未覆盖的课程内容

以下 CMU 15-445 内容未在本项目中实现：

- **SQL Parser / Binder / Planner / Optimizer** — 属于上层 SQL 引擎
- **Query Execution** (Project 3) — 火山模型、Join、Aggregation
- **Disk Manager** — 实际文件 I/O
- **Catalog** — 元数据管理
- **Network / Client-Server** — 通信协议
- **Distributed Transactions** (15-445 后半) — 2PC, Raft, Paxos
