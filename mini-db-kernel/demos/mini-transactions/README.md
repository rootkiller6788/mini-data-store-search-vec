# mini-transactions — 微型事务系统

> 基于 2PL + MVCC + ARIES Recovery 的 ACID 事务演示

## 概述

mini-transactions 是一个用 C99 实现的微型事务系统，演示了数据库事务的三个核心机制：
- **2PL (Two-Phase Locking)**：保证冲突可串行化
- **MVCC (Multi-Version Concurrency Control)**：实现快照隔离，读写不互斥
- **ARIES Recovery**：WAL 日志 + 崩溃恢复，保证 Atomicity 和 Durability

## ACID 属性在本项目中的体现

### Atomicity (原子性)

通过 WAL + Undo 日志保证。事务要么全部提交，要么全部回滚。

```
Begin Txn → [Op1] → [Op2] → ... → Commit/Rollback
              |        |               |
              v        v               v
           WAL Insert/Update/Delete    WAL COMMIT or ABORT
```

崩溃恢复时：
- 已提交的事务：Redo 其所有操作
- 未提交的事务：Undo 其所有操作（利用 before_data 回滚）

### Consistency (一致性)

通过 B+Tree 索引保证数据结构完整性。插入/删除都维持 B+Tree 的所有性质：
- 节点键数在 [ceil(order/2)-1, order-1] 范围内
- 所有叶子在同一深度
- 叶子链表正确连接

### Isolation (隔离性)

通过 **MVCC + 快照隔离** 保证。两阶段锁（2PL）作为备选方案。

#### MVCC 快照隔离

每个事务获取一个快照，包含：
- `snapshot_xmin`：事务开始时最小活跃事务 ID
- `snapshot_xmax`：下一个未分配的事务 ID
- `snapshot_active_txns[]`：当前活跃的事务列表

**版本可见性规则（快照隔离）：**
1. 版本由**自己**创建：`txn_id_begin == my_id` → 可见（除非已被自己删除）
2. 版本由**已提交**的事务创建：`txn_id_begin` 已提交 → 可见
3. 版本由**活跃**事务创建：`txn_id_begin` 在 active list 中 → 不可见
4. 版本已被**已提交**的事务删除：`txn_id_end` 已提交 → 不可见
5. 版本被**未提交**的事务删除：`txn_id_end` 未提交 → 仍可见

```
Tuple Version Chain:
  head → [v3: txn=5, end=0]     ← 最新版本（活跃事务 5）
        → [v2: txn=3, end=5]    ← 被 txn=5 覆盖
        → [v1: txn=1, end=3]    ← 最旧版本（被 txn=3 覆盖）

事务 4 读取（快照：active={3,5}）：
  v3: begin=5 in active → 不可见
  v2: begin=3 in active → 不可见
  v1: begin=1 committed, end=3 not committed → 可见！
  结果：读到 v1
```

#### 2PL (Two-Phase Locking)

如果使用锁管理器：
- **Growing Phase**：事务只能获取锁，不能释放
- **Shrinking Phase**：事务只能释放锁，不能获取

```
Lock Modes:
  SHARED (S)     — 读锁，多个事务可同时持有
  EXCLUSIVE (X)  — 写锁，独占

Compatibility Matrix:
      | S | X |
    S | Y | N |
    X | N | N |
```

**死锁检测：**
使用等待图（Waits-for Graph）检测循环：
- T1 持有 resource A，等待 resource B
- T2 持有 resource B，等待 resource A
- 检测到 cycle → 死锁，需要中止一个事务

**注意：** 本项目使用 MVCC 作为主要隔离机制，2PL 锁管理器作为备选方案。在实际数据库系统（如 PostgreSQL）中，MVCC 和 2PL 常结合使用（MVCC + 行锁）。

### Durability (持久性)

通过 WAL + Checkpoint 保证：
1. 所有修改先写 WAL 日志
2. WAL 日志在事务提交时 force 到磁盘
3. 定期 Checkpoint 减少恢复时间

**恢复时间线：**
```
    Crash Point
        |
    ----+--------------------------------→ Time
    ^       ^           ^
    |       |           |
Checkpoint  |       Last Log
        Redo Start
```

## 事务生命周期

```
1. BEGIN
   └→ 分配 txn_id，获取快照（MVCC）或开始 Growing Phase（2PL）

2. READ(tuple)
   └→ MVCC: 遍历版本链，找到可见版本
   └→ 2PL: 获取 S 锁，读当前版本

3. WRITE(tuple, new_data)
   └→ MVCC: 创建新版本（txn_id_begin = my_id）
   └→ 2PL: 升级到 X 锁或获取 X 锁，替换值

4. COMMIT
   └→ 写 WAL COMMIT 记录
   └→ MVCC: 标记事务为已提交（不再 in active list）
   └→ 2PL: 进入 Shrinking Phase，释放所有锁

5. ABORT
   └→ 写 WAL ABORT 记录（或 Undo WAL 记录）
   └→ MVCC: 标记版本为已中止（版本不会对后续事务可见）
   └→ 释放所有锁

6. GC（后台）
   └→ 清理所有 txn_id_end < min_active_txn 的旧版本
   └→ 版本链压缩
```

## 并发示例

### 场景：两个并发转账

```
时间 →  T1: A=$100→B=$200        T2: A=$100→C=$300
        Begin                    Begin
        Read A ($100)            Read A ($100)
        Write A ($50)            Read A (should see $100, not $50)
        Write B ($250)
        Commit                   Write A ($0)  ← based on snapshot $100
                                 Write C ($400)
                                 Commit
```

**MVCC 快照隔离结果：**
- T1 看到 A=$100（T1 快照在 T2 之前，T2 活跃）
- T2 看到 A=$100（T2 快照在 T1 开始之后，T1 活跃）
- 最终 A=$0（Write-Write Conflict 检测！）

在快照隔离下 T2 的 commit 应该被拒绝（First-Committer-Wins）以避免 Lost Update。本项目简单版本不实现冲突检测，完整实现需要检查 txn_id_end。

## 崩溃恢复场景

### 场景 1：提交前崩溃

```
WAL: [UPDATE A, T1] [UPDATE B, T1] ← crash before COMMIT
Recovery: Undo UPDATE B, Undo UPDATE A
Result: A and B unchanged
```

### 场景 2：提交后崩溃

```
WAL: [UPDATE A, T1] [COMMIT T1] ← crash after COMMIT
Recovery: Redo UPDATE A
Result: A updated, T1 committed
```

### 场景 3：嵌套崩溃

```
WAL: [INS, T1] [UPD-A, T2] [COM, T2] [UPD-B, T1] ← crash
T1: uncommitted  → Undo UPD-B and INS
T2: committed    → Redo UPD-A
```

## 编译与运行

```bash
cd mini-db-kernel
make

# MVCC 基本演示（内嵌于 btree_demo 的部分测试）
./bin/btree_demo

# 锁管理器演示（内嵌于 lock_manager 测试）
# 需要在代码中添加 lm_lock_acquire/release 调用

# WAL 恢复演示
./bin/wal_demo
```

## 与 CMU 15-445 的关系

本实现对应于 CMU 15-445 课程中的以下内容：
- **Lecture 14-15**：Two-Phase Locking (2PL) — `lock_manager.h/c`
- **Lecture 16-17**：MVCC & Snapshot Isolation — `mvcc.h/c`
- **Lecture 18-19**：ARIES Recovery — `wal.h/c`
- **Project 3**：Transaction Manager（部分）
- **Project 4**：Logging & Recovery（部分）

## 参考实现

- PostgreSQL MVCC (HeapTupleHeader, xmin/xmax, snapshot)
- InnoDB MVCC (ReadView, undo log, purge thread)
- CMU 15-445 Bustub (Transaction, LockManager, LogManager)

## 局限性

1. **无 Write-Write 冲突检测**：两个事务同时写同一 tuple 不会触发 abort
2. **简化 Commit/Abort**：未实现 WAL 与 MVCC 版本状态的完全集成
3. **无意向锁（Intention Lock）**：锁粒度只有表级，无行级锁
4. **固定大小版本链**：未实现版本链的深度限制
5. **单线程 GC**：垃圾回收不是并发的
