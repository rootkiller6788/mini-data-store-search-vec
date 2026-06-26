# vector-database-architecture — 向量数据库架构设计

> 参考 FAISS (Meta), Milvus (Zilliz), Weaviate, Qdrant, Chroma

## 目录

1. [概述](#概述)
2. [向量数据库与关系型数据库对比](#向量数据库与关系型数据库对比)
3. [核心架构](#核心架构)
4. [存储层设计](#存储层设计)
5. [索引层设计](#索引层设计)
6. [查询引擎](#查询引擎)
7. [数据流](#数据流)
8. [一致性模型](#一致性模型)
9. [分布式架构](#分布式架构)
10. [API 设计](#api-设计)
11. [监控与运维](#监控与运维)
12. [参考资料](#参考资料)

---

## 概述

向量数据库 (Vector Database) 是专门为高维向量数据的存储和相似性搜索而设计的数据库系统。
与传统的关系型数据库不同，向量数据库的核心能力是**近似最近邻 (ANN) 搜索**。

### 核心特性

- **向量存储**: 高效存储十亿级高维向量 (128-4096 维)
- **ANN 搜索**: 毫秒级返回 top-k 最相似向量
- **混合查询**: 向量相似度 + 标量过滤的组合查询
- **水平扩展**: 数据分片，查询路由
- **持久化**: 磁盘存储，崩溃恢复

---

## 向量数据库与关系型数据库对比

| 特性 | 关系型数据库 (MySQL/PostgreSQL) | 向量数据库 (Milvus/Weaviate) |
|------|-------------------------------|------------------------------|
| 数据模型 | 表格 (行/列) | 向量 + 元数据 |
| 主要查询 | SQL (精确匹配/范围) | ANN (相似度) + 标量过滤 |
| 索引 | B+Tree, Hash | HNSW, IVF, PQ, LSH |
| 排序 | ORDER BY field | ORDER BY vector_distance |
| 扩展性 | 垂直扩展为主 | 水平扩展为主 |
| 典型数据量 | 百万行 | 十亿向量 |
| ACID | 完全支持 | 最终一致性为主 |
| 使用场景 | OLTP, 业务系统 | RAG, 推荐, 搜索 |

### pgvector — 在 PostgreSQL 中融合两者

```sql
-- pgvector 示例
CREATE TABLE documents (
    id SERIAL PRIMARY KEY,
    content TEXT,
    embedding VECTOR(768)
);

CREATE INDEX ON documents USING hnsw (embedding vector_l2_ops);

SELECT id, content, embedding <-> '[0.1, 0.2, ...]'::vector AS distance
FROM documents
WHERE category = 'tech'
ORDER BY distance ASC
LIMIT 10;
```

---

## 核心架构

### 精简架构

```
┌──────────────────────────────────────────────────┐
│                    Client API                      │
│           (REST / gRPC / SDK)                     │
└──────────────────────┬───────────────────────────┘
                       │
┌──────────────────────┴───────────────────────────┐
│                 Query Coordinator                  │
│  ┌─────────────┐  ┌────────────┐ ┌─────────────┐ │
│  │ Query Parser│  │ Plan Gen   │ │ Result Merge│ │
│  └─────────────┘  └────────────┘ └─────────────┘ │
└──────────────────────┬───────────────────────────┘
                       │
┌──────────────────────┴───────────────────────────┐
│                   Index Layer                      │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐   │
│  │ HNSW │ │ IVF  │ │ PQ   │ │ LSH  │ │ Disk │   │
│  │      │ │      │ │      │ │      │ │ ANN  │   │
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘   │
└──────────────────────┬───────────────────────────┘
                       │
┌──────────────────────┴───────────────────────────┐
│                   Storage Layer                    │
│  ┌────────────┐ ┌──────────────┐ ┌──────────────┐│
│  │ WAL        │ │ Vector Store │ │ Meta Store   ││
│  │ (Write-Ahead│ │ (Raw Vectors)│ │ (Attributes) ││
│  │  Log)      │ │              │ │              ││
│  └────────────┘ └──────────────┘ └──────────────┘│
└──────────────────────────────────────────────────┘
```

### 详细组件交互

```
Insert Flow:
  Client → API Gateway
       → Write Coordinator
       → WAL (Write-Ahead Log)
       → Vector Store (append vector)
       → Index Builder (update/schedule index update)
       → Acknowledge to Client

Query Flow:
  Client → API Gateway
       → Query Parser (parse SQL/DSL)
       → Query Planner (choose index, nprobe, ef)
       → Index Search (HNSW/IVF/...)
       → Attribute Filter (apply WHERE clauses)
       → Result Merger (merge from shards)
       → Reranker (optional, re-rank top-N)
       → Return to Client
```

---

## 存储层设计

### 向量存储格式

```
┌─────────────────────────────────────────────┐
│  Segment File Format (.vec)                 │
├─────────────────────────────────────────────┤
│  Header:                                    │
│    - magic (4 bytes)                        │
│    - version (4 bytes)                      │
│    - num_vectors (8 bytes)                  │
│    - dimension (4 bytes)                    │
│    - metric_type (4 bytes)                  │
├─────────────────────────────────────────────┤
│  Vector Data (row-major):                   │
│    vector 0: [v0, v1, ..., v_{d-1}] (4*d)   │
│    vector 1: [v0, v1, ..., v_{d-1}] (4*d)   │
│    ...                                       │
│    vector N-1: [v0, ..., v_{d-1}] (4*d)     │
├─────────────────────────────────────────────┤
│  ID Mapping:                                │
│    external_id[0] → internal_id[0]          │
│    external_id[1] → internal_id[1]          │
│    ...                                       │
├─────────────────────────────────────────────┤
│  Index (postscript):                        │
│    - index type                             │
│    - serialized index data                  │
└─────────────────────────────────────────────┘
```

### WAL (Write-Ahead Log)

```
目的: 保证写入持久性，支持崩溃恢复

┌──────────────────────────────────┐
│  WAL Entry                       │
│  ┌──────────────────────────────┐│
│  │ op_type: INSERT/DELETE/UPDATE││
│  │ vector_id: u64               ││
│  │ vector_data: [f32; dim]      ││
│  │ metadata: JSON blob          ││
│  │ timestamp: u64               ││
│  │ checksum: u32                ││
│  └──────────────────────────────┘│
│  ...                              │
└──────────────────────────────────┘

Rotation Policy:
  - Size > 256MB → rotate
  - Time > 1 hour → rotate
  - After checkpoint → rotate
```

### 段 (Segment) 管理

```
Write Path:
  Active Segment (mutable)
       │
       │ (sealed when size > threshold)
       ↓
  Sealed Segment (immutable)
       │
       │ (background compaction)
       ↓
  Merged Segment (optimized)

Read Path:
  All Segments (active + sealed + merged)
       │
       │ (concurrent scan)
       ↓
  Time-travel Query (point-in-time)
```

### 存储引擎选择

| 存储引擎 | 特点 | 适用场景 |
|----------|------|----------|
| mmap | 零拷贝, OS 管理缓存 | 内存充足 |
| RocksDB | LSM-tree, 压缩好 | 磁盘为主 |
| Parquet | 列存, 压缩好 | 批量分析 |
| 自定义格式 | 完全控制 | 性能极限 |

---

## 索引层设计

### 索引抽象

```c
// 索引接口 (C 风格)
typedef struct Index {
    IndexType type;
    void     *state;
    int       dim;
    MetricType metric;

    // 接口函数指针
    void (*insert)(struct Index *self, int id, const float *vec);
    void (*search)(struct Index *self, const float *query, int k,
                   int *result_ids, float *result_dists);
    void (*remove)(struct Index *self, int id);
    void (*train )(struct Index *self, const float *vecs, int n);
    size_t (*save) (struct Index *self, FILE *fp);
    size_t (*load) (struct Index *self, FILE *fp);
    void (*destroy)(struct Index *self);
} Index;
```

### 多种索引共存

```
应用层:
    根据数据特征和查询需求选择合适的索引类型

┌────────────────────────────────────────┐
│          Index Registry                │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐  │
│  │ Index 1 │ │ Index 2 │ │ Index N │  │
│  │ HNSW    │ │ IVF-PQ  │ │ LSH     │  │
│  │ [col A] │ │ [col B] │ │ [col C] │  │
│  └─────────┘ └─────────┘ └─────────┘  │
└────────────────────────────────────────┘

同一数据可以有多个索引 (不同度量、不同精度)
```

### 默认索引策略

| 数据量 | 推荐索引 |
|--------|---------|
| < 10K | Flat (精确) |
| 10K-100K | HNSW (M=16) |
| 100K-1M | HNSW (M=32) 或 IVF-PQ |
| 1M-10M | IVF-PQ (nlist=4096) |
| > 10M | IVF-PQ + 分区 |

### 距离度量支持

| 度量 | 公式 | 适用场景 |
|------|------|----------|
| L2 (Euclidean) | sqrt(Σ(aᵢ-bᵢ)²) | 通用 |
| IP (Inner Product) | Σ aᵢbᵢ | 推荐系统, 未归一化 |
| Cosine | 1 - cos(θ) | 文本相似度 |
| Jaccard | 1 - |A∩B|/|A∪B| | 集合相似度 (二进制向量) |
| Hamming | Σ aᵢ≠bᵢ | 二进制向量 |

---

## 查询引擎

### 查询 DSL 设计

```
search(
    collection: "documents",
    vector: [0.1, 0.2, ..., 0.768],
    limit: 10,
    metric: "cosine",
    filter: {
        must: [
            { field: "category", op: "eq", value: "tech" },
            { field: "created_at", op: "gte", value: "2024-01-01" }
        ],
        must_not: [
            { field: "status", op: "eq", value: "deleted" }
        ]
    },
    params: {
        nprobe: 16,
        ef: 128
    }
)
```

### 查询计划

```
                    ┌──────────┐
     Query Vector   │  ANN     │
                    │  Search  │
                    └────┬─────┘
                         │  top-k candidates
                         ↓
                    ┌──────────┐
     Scalar Filter  │  Filter  │
                    │  (WHERE) │
                    └────┬─────┘
                         │  filtered candidates
                         ↓
                    ┌──────────┐
                    │  Rerank  │ (optional)
                    │  (exact) │
                    └────┬─────┘
                         │
                         ↓
                    Final Top-K
```

### 预过滤 vs 后过滤

```
预过滤 (Pre-filter):
  - 先用标量条件过滤，再 ANN 搜索
  - 优点: 保证候选都满足条件
  - 缺点: 可能过滤掉真正的近邻

后过滤 (Post-filter):
  - 先 ANN 搜索，再标量过滤
  - 优点: 不丢失近邻
  - 缺点: 可能返回少于 k 个结果

混合策略:
  - 扩大 k (如 k' = k × oversampling_factor)
  - 后过滤 → 如果不够 → 继续扩大搜索
```

---

## 数据流

### 写入路径

```
1. 客户端发送 insert(doc)
2. API Gateway 验证 + 限流
3. Write Coordinator:
   a. 分配内部 ID (自增/雪花)
   b. 写入 WAL
   c. 写入 Vector Store (追加)
   d. 写入 Metadata Store
   e. 提交到 Index Builder 队列
4. 向客户端确认 (ack)
5. 异步: Index Builder 从队列消费并更新索引
```

### 读取路径

```
1. 客户端发送 search(query_vec, k, filter)
2. API Gateway 路由到 Query Coordinator
3. Query Coordinator:
   a. 解析查询
   b. 查询 Metadata (获取活跃的 segments)
   c. 并行发送到所有 Data Nodes
4. Data Node:
   a. 在本地索引搜索
   b. 应用本地过滤
   c. 返回 top-k 候选
5. Query Coordinator:
   a. 合并所有节点的结果
   b. 全局 top-k
   c. Rerank (可选)
   d. 返回给客户端
```

---

## 一致性模型

### 写入一致性

| 级别 | 描述 | 延迟 | 可用性 |
|------|------|------|--------|
| WAL 刷新 | fsync WAL 后返回 | 最高 | 高 |
| 内存写入 | 写入内存后返回 | 低 | 低 (可能丢数据) |
| 索引可见 | 索引更新后返回 | 高 | 最高 |

### 读取一致性

| 级别 | 描述 | 场景 |
|------|------|------|
| 强一致 | 读取最新写入 | 更新后立即查询 |
| 最终一致 | 读取可能不是最新 | 大部分场景 |
| 时间旅行 | 读取某时间点快照 | 审计/回放 |

### MVCC 快照

```
每个写入获得一个全局时间戳 (HLC: Hybrid Logical Clock)
每个查询指定一个时间戳，看到的是该时间点的快照

Segments:
  [0, 100]  [101, 200]  [201, NEXT)    NEXT
  Sealed    Sealed      Active

查询 (ts=150): 读取 [0,100] + [101,200] 中 ts≤150 的部分
查询 (ts=NOW): 读取所有 segments
```

---

## 分布式架构

### 数据分片 (Sharding)

```
策略 1: 哈希分片
  shard = hash(vector_id) % num_shards
  优点: 负载均衡
  缺点: 范围查询困难

策略 2: 范围分片
  shard = vector_id / shard_size
  优点: 范围查询高效
  缺点: 热点问题

策略 3: 一致性哈希
  虚拟节点 + 环形哈希
  优点: 弹性伸缩
```

### 查询路由

```
Broadcast 模式 (小集群):
  Coordinator → All Shards → Merge

Routing 模式 (大集群):
  Coordinator → 查询 nprobe 聚类 → 找到对应 Shards → Merge

Hybrid 模式:
  先用 IVF 粗量化路由 → 只查询目标 Shards
```

### 副本策略

```
Leader-Follower:
  Write → Leader → Replicate → Followers
  Read → Any (Leader or Follower)

Consensus (Raft/Paxos):
  Write → Leader → Quorum → Commit
  Read → Leader (linearizable) or Follower (eventual)
```

---

## API 设计

### REST API 示例

```
POST /v1/collections/{name}/points/insert
{
    "points": [
        {
            "id": 1,
            "vector": [0.1, 0.2, ..., 0.768],
            "payload": {"title": "Doc 1", "category": "tech"}
        }
    ]
}

POST /v1/collections/{name}/points/search
{
    "vector": [0.15, 0.25, ..., 0.77],
    "limit": 10,
    "filter": {
        "must": [{"key": "category", "match": {"value": "tech"}}]
    },
    "params": {"hnsw_ef": 128}
}
```

### gRPC API 示例

```protobuf
service VectorDB {
    rpc Insert(InsertRequest) returns (InsertResponse);
    rpc Search(SearchRequest) returns (SearchResponse);
    rpc Delete(DeleteRequest) returns (DeleteResponse);
    rpc Update(UpdateRequest) returns (UpdateResponse);
    rpc Flush(FlushRequest) returns (FlushResponse);
}
```

---

## 监控与运维

### 关键指标

| 指标 | 描述 | 告警阈值 |
|------|------|----------|
| QPS | 每秒查询数 | > 容量 80% |
| P99 Latency | 99% 查询延迟 | > 100ms |
| Recall@k | 召回率 | < 0.95 |
| Index Build Time | 索引构建时间 | > 1h |
| Memory Usage | 内存使用 | > 可用 80% |
| Disk Usage | 磁盘使用 | > 80% |
| WAL Lag | WAL 堆积 | > 1000 条 |
| Error Rate | 错误率 | > 0.1% |

### 运维操作

```
1. 索引重建: 定期重建索引以优化碎片
2. 压缩 (Compaction): 合并小段，减少元数据
3. 预热 (Warmup): 启动后预加载索引到内存
4. 备份 (Backup): 快照备份到对象存储
5. 迁移 (Migration): 在线迁移数据到新节点
6. 降级 (Degradation): 在资源不足时降低精度保证可用
```

---

## 参考资料

1. FAISS: A library for efficient similarity search — https://github.com/facebookresearch/faiss
2. Milvus Architecture — https://milvus.io/docs/architecture_overview.md
3. Weaviate Architecture — https://weaviate.io/developers/weaviate/architecture
4. Qdrant Documentation — https://qdrant.tech/documentation/
5. "Manu: A Cloud Native Vector Database" (Milvus paper, 2021)
6. "The Case for Learned Index Structures" (Kraska et al., 2018)
7. "Neural Vector Spaces for Unifying Information Retrieval" (Bruch et al., 2023)
