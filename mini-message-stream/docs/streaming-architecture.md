# mini-message-stream — Streaming Architecture

## 流处理架构

### 1. 系统层次结构

```
┌─────────────────────────────────────────────────────────┐
│                      Application Layer                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │ Producer Demo │  │Consumer Demo │  │ Broker Demo  │   │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘   │
├─────────┼──────────────────┼──────────────────┼──────────┤
│         │    Client API    │                  │          │
│  ┌──────┴───────┐  ┌──────┴───────┐          │          │
│  │ ProducerClient│  │ConsumerGroup │          │          │
│  └──────┬───────┘  └──────┬───────┘          │          │
├─────────┼──────────────────┼──────────────────┼──────────┤
│         │    Broker Tier   │                  │          │
│  ┌──────┴──────────────────┴──────────────────┴───────┐  │
│  │                   Broker Server                     │  │
│  │  ┌─────────┐  ┌──────────────┐  ┌──────────────┐  │  │
│  │  │ Produce  │  │    Fetch     │  │   Metadata   │  │  │
│  │  │ Handler  │  │   Handler    │  │   Handler    │  │  │
│  │  └────┬─────┘  └──────┬───────┘  └──────┬───────┘  │  │
│  └───────┼────────────────┼──────────────────┼─────────┘  │
├──────────┼────────────────┼──────────────────┼────────────┤
│          │  Storage Tier  │                  │            │
│  ┌───────┴────────────────┴──────────────────┴─────────┐  │
│  │                 Topic / Partition                    │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐          │  │
│  │  │ Segment 0│  │ Segment 1│  │ Segment N│ (active) │  │
│  │  └──────────┘  └──────────┘  └──────────┘          │  │
│  └──────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────┤
│                   Offset Management                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  OffsetStore: group → topic → partition → offset     │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 2. 数据流路径

#### 2.1 写入路径 (Write Path)

```
User Code
  │
  ▼
producer_send(key, value, headers)
  │
  ├── producer_partitioner(key) → partition_id
  │
  ├── batch accumulation (pending_batch)
  │
  ├── [batch full OR flush triggered]
  │
  ▼
broker_handle_produce(topic, partition, key, value)
  │
  ├── look up topic → partition
  ├── verify leader broker
  │
  ▼
partition_append(key, value, headers)
  │
  ├── get active segment
  ├── [segment full] → partition_roll_segment()
  │
  ▼
Record written to segment → return offset
```

#### 2.2 读取路径 (Read Path)

```
User Code
  │
  ▼
group_join() → group_sync() → get assignment
  │
  ▼
offset_fetch(group, topic, partition) → start_offset
  │
  ▼
broker_handle_fetch(topic, partition, start_offset)
  │
  ├── look up topic → partition
  │
  ▼
partition_read(start_offset, max_records)
  │
  ├── search segments for matching offset
  ├── copy records to output buffer
  │
  ▼
return records → user processes → offset_commit()
```

### 3. 重平衡时序图

```
Consumer-A     Consumer-B     Consumer-C     Coordinator
    │              │              │              │
    │              │     JoinGroup              │
    │              │───────────────────────────▶│
    │              │              │              │
    │     JoinGroup              │              │
    │──────────────────────────────────────────▶│
    │              │              │              │
    │              │              │ JoinGroup   │
    │              │              │────────────▶│
    │              │              │              │
    │              │  选举 Leader (Consumer-A)   │
    │◀─────────────────┼──────────┼──────────────│
    │              │              │ 成员列表      │
    │              │              │              │
    │═══ A 执行分区分配 ═══════│              │
    │              │              │              │
    │     SyncGroup(分配结果)     │              │
    │──────────────────────────────────────────▶│
    │              │              │              │
    │              │  SyncGroupResponse          │
    │◀─────────────────┼──────────┼──────────────│
    │              │◀─────────────┼──────────────│
    │              │              │◀─────────────│
    │              │              │              │
    │   generation_id++, is_rebalancing=false    │
```

### 4. 组件交互矩阵

|              | Topic | Partition | Segment | Producer | Consumer | Broker | OffsetStore |
|-------------|-------|-----------|---------|----------|----------|--------|-------------|
| Topic       |   —   | owns      | —       | writes   | reads    | —      | —           |
| Partition   | —     | —         | owns    | —        | —        | manages | —           |
| Segment     | —     | —         | —       | —        | —        | —      | —           |
| Producer    | target| router    | —       | —        | —        | sends   | —           |
| Consumer    | sub   | assigned  | —       | —        | —        | fetches | commits     |
| Broker      | hosts | manages   | —       | accepts  | serves   | —       | —           |
| OffsetStore | —     | —         | —       | —        | —        | —       | —           |

### 5. 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 存储引擎 | 内存数组（模拟） | 简化实现，便于调试 |
| 副本机制 | 基础 Leader 管理 | 演示 ISR 概念 |
| 分配策略 | Range + Round-Robin | 两种策略覆盖常见场景 |
| ACK 模式 | None/Leader/All | 展示不同可靠性级别 |
| Offset 存储 | 独立 OffsetStore | 解耦偏移量管理 |
| 客户端模拟 | 同步调用 | 避免复杂事件循环 |
