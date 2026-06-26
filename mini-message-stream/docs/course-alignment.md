# mini-message-stream — Course Alignment

## 课程对齐说明

本模块 `mini-message-stream` 与大学计算机科学课程中消息系统与分布式流处理相关的知识点对齐。

### 对标课程主题

| 课程主题 | 本模块覆盖 | 文件 |
|---------|-----------|------|
| 消息队列基础 | Topic/Partition 模型 | `include/topic_partition.h` |
| 生产者-消费者模式 | Producer/Consumer 客户端 | `include/producer_client.h`, `include/consumer_group.h` |
| 分布式系统 | Broker 架构、副本机制 | `include/broker.h` |
| 一致性协议 | Offset 管理、Lag 计算 | `include/offset_manager.h` |
| 分区策略 | Range / Round-Robin 分配 | `src/consumer_group.c` |
| 容错设计 | 心跳、会话超时、重平衡 | `src/consumer_group.c` |
| 数据持久化 | Log Segment 管理 | `src/topic_partition.c` |

### 理论知识覆盖

1. **消息传递语义** (Message Delivery Semantics)
   - At-Most-Once: `PRODUCER_ACK_NONE`
   - At-Least-Once: `PRODUCER_ACK_LEADER`, commit before/after processing
   - Exactly-Once: 幂等性 + 事务性写入

2. **分区与并行** (Partitioning & Parallelism)
   - 基于 Key Hash 的分区策略 (`producer_partitioner`)
   - Round-Robin 分区
   - 分区是并行度的单位

3. **消费者组协议** (Consumer Group Protocol)
   - JoinGroup → SyncGroup → Heartbeat
   - Generation ID 防止僵尸消费者
   - Rebalance: Eager Protocol

4. **偏移量管理** (Offset Management)
   - Offset Commit: 消费者记录消费位置
   - Offset Fetch: 恢复时获取上次位置
   - Offset Reset: 重置到最早或最新

5. **副本与可靠性** (Replication & Reliability)
   - Leader/Follower 模型
   - ISR (In-Sync Replicas)
   - ACK 机制: None, Leader, All

### 算法复杂度分析

| 操作 | 时间复杂度 | 空间复杂度 |
|------|-----------|-----------|
| `partition_append` | O(1) | O(1) |
| `partition_read` | O(N) N=record count | O(K) K=out_count |
| `producer_partitioner` | O(L) L=key length | O(1) |
| `group_assign_range` | O(P+M) P=partitions M=members | O(1) |
| `group_assign_round_robin` | O(P) P=partitions | O(1) |
| `offset_commit` | O(G+T+P) G=groups T=topics P=partitions | O(1) |

### 实践练习建议

1. **基础练习**：修改 `producer_demo.c` 增加更多分区，观察分布均匀性
2. **进阶练习**：给 `consumer_demo.c` 增加故障注入（随机移除成员）
3. **高级练习**：实现 Sticky 分配策略替代 Range/Round-Robin
4. **扩展练习**：将内存存储替换为文件持久化
