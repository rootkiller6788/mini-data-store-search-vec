# mini-kafka-broker — Kafka Broker Internals

> 参考 Kafka Internals, Apache Pulsar, DistributedLog

## 1. 概述

`mini-kafka-broker` 演示了一个简化的消息代理服务器实现，借鉴 Apache Kafka 的内部架构设计。本模块模拟了 Kafka 的核心组件：Topic（主题）、Partition（分区）、Log Segment（日志段）以及基础的控制面功能。

Kafka 是一个分布式流媒体平台，最初由 LinkedIn 开发并开源于 Apache 基金会。它的设计围绕三个核心能力：发布和订阅记录流（消息队列）、以容错持久化方式存储记录流、以及在记录产生时实时处理。

### 为什么选择 Kafka 的架构？

- **高吞吐**：顺序磁盘 I/O + 零拷贝（zero-copy）技术
- **持久化**：所有消息写入磁盘，不依赖内存缓存
- **水平扩展**：分区机制允许数据分布在多台 broker 上
- **消费者组**：支持多个消费者协同消费，保证消息有序处理

## 2. Topic 与 Partition

### 2.1 Topic

Topic 是消息的逻辑分类，类似于数据库的表。生产者将消息发布到特定 Topic，消费者从 Topic 订阅消息。

```c
typedef struct {
    char        name[MAX_TOPIC_NAME_LEN];
    Partition   partitions[MAX_PARTITIONS];
    int         partition_count;
    int64_t     retention_ms;
} Topic;
```

核心属性：
- `name`：Topic 名称，全局唯一
- `partitions`：分区数组，每个分区是独立的有序日志
- `partition_count`：分区数量，决定并行度
- `retention_ms`：消息保留时长（毫秒），过期后自动清理

### 2.2 Partition

分区是 Topic 的物理分片，每个分区是一个追加写（append-only）的不可变日志。

```c
typedef struct {
    int         id;
    int         leader_broker;
    int         replicas[MAX_REPLICAS];
    int         replica_count;
    LogSegment  segments[MAX_SEGMENTS];
    int         segment_count;
    int         active_segment_idx;
} Partition;
```

核心概念：
- **Leader 副本**：负责处理所有对该分区的读写请求
- **Follower 副本**：被动从 Leader 复制数据，作为冗余
- **ISR（In-Sync Replicas）**：与 Leader 保持同步的副本集合

分区保证了：
- **顺序性**：同一分区内的消息严格有序
- **可扩展性**：分区可分布在不同的 broker 上
- **容错性**：通过副本机制实现故障转移

### 2.3 Log Segment

每个分区由多个连续的 Log Segment（日志段）组成。当前活跃的 segment 接收新的写入请求。

```c
typedef struct {
    int64_t  base_offset;
    Record   records[MAX_RECORDS_PER_SEG];
    int      size;
    int      is_active;
} LogSegment;
```

**Segment 滚动策略**：
- 当 segment 达到大小上限（如 1GB）时，关闭当前 segment，创建新 segment
- 旧 segment 可用于压缩（compaction）和清理（retention）

### 2.4 Record

Record 是消息的基本单位：

```c
typedef struct {
    int64_t  offset;
    int64_t  timestamp;
    char     key[MAX_KEY_LEN];
    char     value[MAX_VALUE_LEN];
    char     headers[MAX_HEADER_LEN];
} Record;
```

- `offset`：消息在分区内的唯一有序标识
- `timestamp`：消息产生时间
- `key`：用于分区路由计算
- `value`：消息体
- `headers`：键值对元数据

## 3. 副本机制

### 3.1 Leader 与 Follower

每个分区的 Leader 负责：
1. 接收生产者写入请求
2. 维护 HW（High Watermark）和 LEO（Log End Offset）
3. 协调 Follower 同步

Follower 的角色：
1. 从 Leader 拉取新消息
2. 维护各自的 LEO
3. 在 Leader 故障时参与选举

### 3.2 ISR 机制

ISR（In-Sync Replicas）是 Kafka 的核心可靠性保证：

- Follower 必须在一定时间内跟上 Leader 的写入
- 如果 Follower 落后太多（通过 `replica.lag.time.max.ms` 控制），会被移出 ISR
- 只有 ISR 中的副本才有资格成为新 Leader
- `ack=all` 时，消息必须被 ISR 中所有副本确认才算提交

### 3.3 控制器（Controller）

控制器是 Kafka 集群中一个特殊的 Broker：

- 负责管理分区和副本的状态
- 执行分区 Leader 选举
- 向其他 Broker 通知集群元数据变更
- 通过 ZooKeeper / KRaft 实现选举

## 4. 数据流与请求处理

### 4.1 生产请求（Produce Request）

```
Producer → Broker → Partition Leader → Segment → Disk
                  → Replicate to Followers
```

处理流程：
1. Producer 发送 ProduceRequest 到 Broker
2. Broker 查找 Topic 的 Leader 分区
3. 将消息追加到活跃 Segment
4. 根据 `ack` 配置决定何时响应：
   - `ack=0`：不等待确认，立即返回
   - `ack=1`：Leader 写入后返回
   - `ack=all/-1`：等待所有 ISR 确认后返回

### 4.2 获取请求（Fetch Request）

```
Consumer → Broker → Partition → Read from offset → Return records
```

处理流程：
1. Consumer 发送 FetchRequest，指定 offset
2. Broker 在分区 Segment 中按 offset 查找
3. 返回一批记录给 Consumer
4. Consumer 处理完毕后提交 offset

### 4.3 元数据请求（Metadata Request）

返回集群拓扑信息，帮助客户端发现分区的 Leader：

```
Client → Broker → Return { topic, partitions, leader, replicas }
```

## 5. 性能设计要点

### 5.1 顺序 I/O

Kafka 将消息写入分区日志，使用追加写（append）模式：
- 机械磁盘的顺序写入速度可达 600MB/s+
- 避免了传统数据库的随机 I/O 瓶颈
- 利用操作系统的页缓存（Page Cache）加速读取

### 5.2 零拷贝

Kafka 使用 `sendfile()` 系统调用，数据从磁盘直接传输到网络 socket：
- 数据不经过用户空间
- 降低 CPU 占用
- 大幅提升吞吐

### 5.3 批量与压缩

- **批量发送**：Producer 累积多条消息后批量发送
- **消息压缩**：支持 GZIP、Snappy、LZ4、Zstd 压缩
- **linger.ms**：控制批量等待时间，权衡延迟与吞吐

## 6. 实现对比

| 功能 | Kafka | mini-kafka-broker |
|------|-------|-------------------|
| 多 Broker 集群 | 完全支持 | 单 Broker 或简化多 Broker |
| 分区机制 | 完整 ISR + 选举 | 基础 Leader 管理 |
| 日志段管理 | 自动滚动 + 清理 | 自动滚动 |
| 消费者组 | 完整协议 | 基础重平衡 |
| 副本同步 | Leader→Follower 推送 | 基础模拟 |
| 持久化 | 磁盘 + 页缓存 | 内存（模拟） |

## 7. 参考资源

- Apache Kafka Documentation: https://kafka.apache.org/documentation/
- Kafka Internals (Confluent): https://www.confluent.io/blog/
- Designing Data-Intensive Applications, Chapter 5 (Replication)
- Apache Pulsar Architecture: https://pulsar.apache.org/docs/
- DistributedLog (Twitter): https://bookkeeper.apache.org/distributedlog/

## 8. 扩展方向

1. **多 Broker 集群**：实现完整的集群通信协议
2. **ISR 管理**：动态维护 ISR 列表
3. **Leader 选举**：基于 ZooKeeper/KRaft 的选举算法
4. **日志压缩**：Key-based compaction for compacted topics
5. **事务支持**：Exactly-once 语义实现
