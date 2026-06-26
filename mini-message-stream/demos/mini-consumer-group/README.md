# mini-consumer-group — 消费者组与重平衡协议

> 参考 Kafka Consumer Group Protocol, Apache Pulsar Subscription, DistributedLog

## 1. 概述

消费者组（Consumer Group）是 Kafka 实现消息并行消费和容错的核心机制。多个消费者实例可以组成一个消费者组，协同消费一个或多个 Topic。每个分区只能被组内的一个消费者消费。

本模块实现了消费者组的基础功能：
- 组成员管理（加入 / 离开）
- 分区分配策略（Range、Round-Robin）
- 重平衡协议（Rebalance Protocol）
- 消费者心跳与会话超时
- 偏移量管理（Offset Management）

## 2. 消费者组架构

### 2.1 核心数据结构

```c
typedef struct {
    char              group_id[MAX_GROUP_ID_LEN];
    ConsumerMember    members[MAX_GROUP_MEMBERS];
    int               member_count;
    char              subscribed_topics[MAX_SUBSCRIBED_TOPICS][128];
    int               subscribed_count;
    char              coordinator[128];
    int               generation_id;
    PartitionStrategy strategy;
    int               is_rebalancing;
} ConsumerGroup;
```

### 2.2 组成员

每个 ConsumerMember 代表组内的一个消费者实例：

```c
typedef struct {
    char       member_id[MAX_MEMBER_ID_LEN];
    int        assigned_partitions[MAX_PARTITIONS];
    int        assigned_count;
    int64_t    last_heartbeat;
    int        session_timeout_ms;
    int        is_active;
} ConsumerMember;
```

- `member_id`：消费者唯一标识
- `assigned_partitions`：当前分配的分区列表
- `last_heartbeat`：上次心跳时间
- `session_timeout_ms`：会话超时时间（默认 30000ms）
- `is_active`：是否活跃

## 3. 重平衡协议（Rebalance Protocol）

### 3.1 什么是重平衡？

重平衡是消费者组在成员变化时重新分配分区的过程，触发条件包括：

1. **新成员加入**：组内消费者数量增加
2. **成员离开**：消费者主动退出或崩溃（session 超时）
3. **Topic 分区数变化**：订阅的 Topic 增加了新分区

### 3.2 重平衡流程

```
Phase 1: JoinGroup
  ├── Consumer → GroupCoordinator: JoinGroupRequest
  ├── Coordinator: 选举 Group Leader
  └── Coordinator → Members: JoinGroupResponse (包含成员列表)

Phase 2: SyncGroup
  ├── Leader: 执行分区分配策略
  ├── Leader → Coordinator: SyncGroupRequest (分配结果)
  └── Coordinator → Members: SyncGroupResponse (各自的分配)
```

### 3.3 Generation ID

每次成功重平衡都会生成新的 `generation_id`（世代 ID）：
- 用于区分不同的重平衡周期
- 消费者在 commit offset 时必须携带当前 generation_id
- 防止旧的 stale consumer 提交过期 offset

### 3.4 本实现的简化

```
group_join()     → 注册成员，设置 is_rebalancing = true
group_sync()     → 执行分配策略，清除 is_rebalancing
group_heartbeat()→ 更新心跳，检查超时
group_leave()    → 移除成员，触发重新平衡
```

## 4. 分区分配策略

### 4.1 Range（范围分配）

将连续范围的分区分给每个消费者：

```
分区: 0, 1, 2, 3, 4, 5
成员: A, B, C

分配: A → [0, 1]
      B → [2, 3]
      C → [4, 5]
```

算法：每个消费者获得 `total_partitions / member_count` 个分区，余数从第一个消费者开始分配。

### 4.2 Round-Robin（轮询分配）

将分区依次轮询分配给消费者：

```
分区: 0, 1, 2, 3, 4, 5
成员: A, B, C

分配: A → [0, 3]
      B → [1, 4]
      C → [2, 5]
```

算法：遍历所有分区，依次循环分配给下一个消费者。

### 4.3 策略选择

| 策略 | 适用场景 | 分配均匀度 |
|------|---------|-----------|
| Range | 分区数 >> 消费者数 | 可能不均（余数全局放前面）|
| Round-Robin | 消费者数可整除分区数 | 完全均匀 |
| Sticky | 尽量保持原有分配 | 减少分区迁移 |

## 5. 偏移量管理（Offset Management）

### 5.1 偏移量语义

每个消费者通过 `offset` 跟踪其消费进度：

```
offset = 0 ──→ 第一条消息
offset = N ──→ 第 N+1 条消息
```

### 5.2 提交模式

| 模式 | 行为 | 风险 |
|------|------|------|
| 自动提交 | 固定间隔自动提交 | 可能重复消费 |
| 手动同步提交 | 处理完消息后显式提交 | 阻塞线程 |
| 手动异步提交 | 非阻塞提交 | 需要处理回调 |

### 5.3 偏移量重置策略

- `offset_reset_to_earliest`：重置到最早可用 offset（0）
- `offset_reset_to_latest`：重置到最新 offset（log end offset）

## 6. 消费者 Lag 监控

### 6.1 什么是 Consumer Lag？

Consumer Lag = 分区末尾 offset（Log End Offset）— 消费者已提交 offset

```
最近写入: offset 1000  ← LEO (Log End Offset)
已消费:   offset 850   ← Committed Offset
Lag:      150           ← 未消费消息数
```

### 6.2 Lag 健康状态

| Lag 范围 | 状态 | 建议 |
|---------|------|------|
| < 10 | 健康 | 正常运行 |
| 10-100 | 中等 | 关注趋势 |
| > 100 | 告警 | 需要扩容或优化 |

### 6.3 Lag 计算公式

```c
int64_t consumer_lag_calculate(int64_t end_offset, int64_t committed_offset)
{
    if (committed_offset < 0) return end_offset;
    if (committed_offset > end_offset) return 0;
    return end_offset - committed_offset;
}
```

## 7. Exactly-Once 语义

### 7.1 三种语义

| 语义 | 含义 | 挑战 |
|------|------|------|
| At-Most-Once | 消息最多处理一次，可能丢失 | 简单，无需状态管理 |
| At-Least-Once | 消息至少处理一次，可能重复 | 需要幂等处理 |
| Exactly-Once | 消息恰好处理一次 | 需要事务性协调 |

### 7.2 Kafka 的 Exactly-Once 实现

- **幂等 Producer**：`enable.idempotence=true`，通过 Producer ID + Sequence Number 去重
- **事务性 Producer**：`transactional.id`，跨多个 Topic/Partition 的原子写入
- **Read-Process-Write**：消费-处理-生产 模式的事务保证

### 7.3 本实现中的简化

- 偏移量在 OffsetStore 中持久保存
- 消费者先处理消息再提交 offset（At-Least-Once）
- 需要应用程序保证幂等性来实现 Exactly-Once

## 8. Group Coordinator

### 8.1 协调器角色

Group Coordinator 是负责管理消费者组的 Broker：
- 维护组成员列表
- 触发并管理重平衡
- 接收偏移量提交请求
- 存储组元数据（在内部 Topic `__consumer_offsets` 中）

### 8.2 协调器发现

消费者通过 `FindCoordinator` API 找到负责其组 ID 的 Broker：
```
group_id → hash → __consumer_offsets partition → partition leader = Coordinator
```

## 9. 会话管理与心跳

### 9.1 心跳机制

- 消费者定期向协调器发送心跳（`heartbeat.interval.ms`）
- 协调器跟踪每个消费者的心跳时间
- 如果 `session.timeout.ms` 内未收到心跳，标记消费者为死亡
- 触发重平衡，重新分配分区

### 9.2 实现细节

```c
int group_heartbeat(ConsumerGroup *g, const char *member_id)
{
    // 1. 查找成员
    // 2. 计算上次心跳到现在的间隔
    // 3. 如果超过 session_timeout_ms，标记 inactive 并触发重平衡
    // 4. 更新 last_heartbeat
}
```

## 10. 参考资源

- Kafka Consumer Group Protocol: https://kafka.apache.org/documentation/
- Consumer Group Rebalance Design (KIP-429)
- Apache Pulsar Subscription Types: https://pulsar.apache.org/docs/
- DistributedLog Documentation: https://bookkeeper.apache.org/distributedlog/
- "Kafka: The Definitive Guide", Chapters 3-4 (Consumers)
