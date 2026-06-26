# mini-message-stream — 消息流处理 (C 语言实现)

> 参考 Kafka Internals, Apache Pulsar, DistributedLog

## 概述

`mini-message-stream` 是一个用 C99 编写的简化消息流处理库，模拟 Apache Kafka 的核心组件：Topic/Partition 模型、Producer/Consumer 客户端、Broker 服务器、消费者组与重平衡协议、偏移量管理。

## 快速开始

### 构建

```sh
make
```

### 运行示例

```sh
make run-producer    # 生产者示例：创建 Topic，发送 100 条消息
make run-consumer    # 消费者组示例：订阅、消费、提交、重平衡
make run-broker      # 单 Broker 示例：完整的生产-消费-偏移量循环
make run-all         # 运行所有示例
```

## 项目结构

```
mini-message-stream/
├── include/
│   ├── topic_partition.h    # Topic/Partition/LogSegment/Record 模型
│   ├── producer_client.h    # 生产者客户端（批量发送、ACK 模式）
│   ├── consumer_group.h     # 消费者组（加入、同步、重平衡、心跳）
│   ├── broker.h             # Broker 服务器（生产/获取/元数据）
│   └── offset_manager.h     # 偏移量管理（提交/读取/重置/Lag）
├── src/
│   ├── topic_partition.c    # 分区写入、读取、Segment 滚动
│   ├── producer_client.c    # 批量累积、分区路由（hash/round-robin）
│   ├── consumer_group.c     # 成员管理、Range/Round-Robin 分配
│   ├── broker.c             # 请求处理、Topic 注册
│   └── offset_manager.c     # 偏移量存储、Lag 计算与打印
├── examples/
│   ├── producer_demo.c      # 100 条消息生产演示
│   ├── consumer_demo.c      # 消费者组消费演示
│   └── broker_demo.c        # 单 Broker 完整流程演示
├── demos/
│   ├── mini-kafka-broker/README.md     # Kafka Broker 内部原理
│   └── mini-consumer-group/README.md   # 消费者组与重平衡协议
├── docs/
│   ├── course-alignment.md             # 课程对齐
│   └── streaming-architecture.md       # 流处理架构
├── Makefile
└── README.md
```

## 核心组件

### Topic & Partition

```c
Topic *t = topic_create("orders", 4, 86400000);  // 4 分区, 24h 保留
int64_t offset = partition_append(&t->partitions[0], "key1", "value1", "");
```

### Producer

```c
Producer *p = producer_create("prod-1", "localhost:9092", "orders",
                              PRODUCER_ACK_LEADER, 5);
producer_send(p, "user-42", "{\"order\":100}", "");
producer_flush(p);
```

### Consumer Group

```c
ConsumerGroup *g = group_create("my-group", "broker:9092", STRATEGY_RANGE);
group_join(g, "consumer-A", 30000);
group_sync(g, topic_list, 1);
group_heartbeat(g, "consumer-A");
```

### Broker

```c
Broker *b = broker_create(1, "localhost", 9092, 1);
broker_register_topic(b, topic);
broker_handle_produce(b, "orders", 0, "key", "value", "", &offset);
```

### Offset Management

```c
offset_commit(os, "my-group", "orders", 0, offset);
print_consumer_lag("my-group", "orders", 0, end_offset, committed);
```

## ACK 模式

| 值 | 含义 | 延迟 | 可靠性 |
|----|------|------|--------|
| `PRODUCER_ACK_NONE (0)` | 不等待确认 | 最低 | 可能丢失 |
| `PRODUCER_ACK_LEADER (1)` | Leader 确认 | 中等 | 默认级别 |
| `PRODUCER_ACK_ALL (-1)` | 所有 ISR 确认 | 最高 | 最高可靠性 |

## 分区策略

- **Key Hash**：`producer_partitioner` 对 key 使用 DJB2 hash，映射到分区
- **Null Key**：使用 `time(NULL) % num_partitions`（近似 Round-Robin）

## 消费者组分配策略

- **Range**：连续范围分给每个消费者，适合分区数远大于消费者数
- **Round-Robin**：轮流分配，保证均匀性

## License

MIT
