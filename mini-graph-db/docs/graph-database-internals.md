# 图数据库内部架构 — 从 mini-graph-db 到 Neo4j

> 参考 Designing Data-Intensive Applications (Kleppmann), Neo4j Internals, TigerGraph Architecture

## 架构层次概览

```
┌─────────────────────────────────────────────┐
│         查询语言层 (Query Language)          │
│  Cypher / Gremlin / SPARQL / openCypher     │
├─────────────────────────────────────────────┤
│         查询处理器 (Query Processor)         │
│  解析器 → 优化器 → 执行计划 → 运行时         │
├─────────────────────────────────────────────┤
│         索引层 (Index Layer)                 │
│  标签索引 | 属性索引 | 全文索引 | 空间索引    │
├─────────────────────────────────────────────┤
│         图引擎 (Graph Engine)                │
│  邻接表 | 遍历引擎 | 图算法 | 事务管理        │
├─────────────────────────────────────────────┤
│         存储引擎 (Storage Engine)            │
│  记录存储 | WAL | 缓存 | 压缩 | 文件管理      │
└─────────────────────────────────────────────┘
```

mini-graph-db 实现了前四层的核心子集。

## 1. 存储引擎

### 属性图存储格式

工业级图数据库通常使用两种存储方式：

#### 原生图存储 (Neo4j)
```
[Node Store]
| node_id | first_rel_id | first_prop_id | labels |

[Relationship Store]
| rel_id | from_node | to_node | type | prev_rel_from | next_rel_from |
| prev_rel_to | next_rel_to | first_prop_id |

[Property Store]
| prop_id | key | value | next_prop_id |
```

**关键设计**:
- 固定大小记录：节点 15 字节，关系 34 字节
- 双向链表：每个节点维护入边和出边的双向链表
- 属性链：属性通过单向链表连接
- 遍历复杂度：O(1) 访问邻居，真正实现"无索引邻接" (Index-Free Adjacency)

**图 1: Neo4j 节点存储布局**
```
Node Record (15 bytes):
┌──────────┬──────────────┬────────────────┬──────────┐
│ inUse(1) │ nextRelId(5) │ nextPropId(5)  │ labels(4)│
└──────────┴──────────────┴────────────────┴──────────┘

Relationship Record (34 bytes):
┌──────────┬───────────┬───────────┬──────────┬─────────────┬─────────────┬─────────────┬─────────────┬──────────────┐
│inUse(1)  │firstNode  │secondNode │ type(5)  │firstPrevRel │firstNextRel │secondPrevRel│secondNextRel│nextPropId(5)│
│          │    (5)    │    (5)    │          │    (5)     │    (5)      │    (5)      │    (5)      │              │
└──────────┴───────────┴───────────┴──────────┴─────────────┴─────────────┴─────────────┴─────────────┴──────────────┘
```

#### LSM 树 存储 (JanusGraph, Dgraph)
- 基于 RocksDB/LevelDB
- 写优化：写入内存表，定期合并到磁盘
- 图表示为邻接表的键值对

### mini-graph-db 的存储实现

```c
// src/property_graph.c - 数组存储
Node nodes[MAX_NODES];       // 4096 个节点
Edge edges[MAX_EDGES];       // 16384 条边
AdjList *adjacency;          // 动态邻接表
```

- **优点**: 实现简单，适合教学
- **弱点**: 固定容量，非持久化

### WAL (Write-Ahead Logging)

所有变更先写入追加日志，再应用到主存储。

```
WAL Entry Format:
[LSN(8) | TX_ID(8) | OP_TYPE(1) | DATA(varlen) | CHECKSUM(4)]

OP_TYPE:
  N = Node Created
  E = Edge Created
  U = Property Updated
  D = Deleted
```

**恢复流程**:
1. 找到最后一个检查点
2. 重放检查点之后的所有 WAL 条目
3. 未提交事务回滚 (UNDO)

## 2. 图引擎 — 遍历

### 指针追逐 (Pointer Chasing)

原生图存储的核心优势：遍历一条边仅需解引用一个指针。

```
Neo4j 遍历:
  node_record → read(15B)
  relations_linked_list → 指针遍历
  每跳: ~2-3 次内存访问

关系数据库 JOIN:
  每跳: 索引查找 (B-tree traversal)
  log_B(N) 次磁盘页访问
```

### 遍历策略

#### 广度优先 (BFS)
- **队列**: 64KB 环形缓冲区
- **用例**: 最短路径，k-跳邻居
- **内存**: O(V)

#### 深度优先 (DFS)
- **栈**: 递归或显式
- **用例**: 存在性查询，路径查找
- **内存**: O(depth) → O(log V) inspired

#### 双向 BFS
- 同时从源和目标出发
- 相遇时停止
- 复杂度: O(b^(d/2)) vs O(b^d)

### mini-graph-db 的遍历实现
```c
// src/graph_traversal.c
PathResult traverse_bfs(...)
PathResult traverse_dfs(...)
PathResult traverse_dijkstra(...)
```

当前使用简单的数组实现，适合教育目的。

## 3. 查询引擎

### 查询处理流水线

```
Query String
    │
    ▼
┌──────────┐
│  Lexer   │  词法分析: 识别令牌 (IDENT, LABEL, EDGE, LPAREN, ...)
└────┬─────┘
     │
     ▼
┌──────────┐
│  Parser  │  语法分析: 构建 AST (抽象语法树)
└────┬─────┘
     │
     ▼
┌────────────┐
│  Semantic  │  语义分析: 类型检查, 标签存在性验证
└────┬───────┘
     │
     ▼
┌──────────────┐
│  Optimizer   │  查询优化: 重排序, 谓词下推, 索引选择
└────┬─────────┘
     │
     ▼
┌──────────────┐
│  Planner     │  执行计划: 生成算子 DAG
└────┬─────────┘
     │
     ▼
┌──────────────┐
│  Runtime     │  执行: 迭代算子, 产出结果
└──────────────┘
```

### mini-graph-db 的查询处理器
```c
// src/cypher_like.c
bool cypher_parse(...)      // 词法+语法+语义 (合并)
QueryResult cypher_match(...)  // 计划+执行 (合并)
```

实现简化了流水线，适合教学理解。

### 查询优化: 基于成本的优化 (CBO)

```
MATCH (a:Person)-[:KNOWS]->(b:Person)-[:LIVES_IN]->(c:City)

优化前:
  for each node Na (4096):
    for each edge Na→Nb (avg 10):
      for each edge Nb→Nc (avg 10):
        check labels

优化后 (选择率驱动):
  City nodes (估 50) → LIVES_IN inbound → Person → KNOWS → Person
  50 × 2 × 5 = 500 次操作 vs 409,600
```

## 4. 索引

### 索引类型

| 索引类型 | 用途 | 数据结构 |
|---------|------|---------|
| 标签索引 | 快速定位有某标签的节点 | 哈希表 |
| 属性索引 | 属性等值/范围查找 | B+树 / 哈希 |
| 全文索引 | 文本搜索 | 倒排索引 (Lucene) |
| 空间索引 | 地理位置查询 | R-树 / Geohash / 网格 |

### mini-graph-db 的索引实现
```c
// src/graph_index.c
NodeIndex     → 标签 → 节点列表
EdgeIndex     → 边类型 → 边列表
PropertyIndex → 键值 → 节点列表
SpatialIndex  → 网格 → 节点列表
```

## 5. 事务与并发

### ACID 在图中的实现

#### A — 原子性
- WAL + Checkpoint
- Undo 日志

#### C — 一致性
- 约束验证 (Schema, 唯一性)
- 触发后检查

#### I — 隔离性
- **乐观并发控制**: 提交时检查冲突
- **悲观锁**: 锁节点/边

锁层级：
```
Graph Lock → Node Lock → Edge Lock → Property Lock
```

#### D — 持久性
- WAL 同步写入
- fsync 保证落盘

### Neo4j 的隔离级别
- 默认: Read Committed
- 可配置: Serializable (通过锁升级)

## 6. 分布式图数据库

### 图分区策略

#### 边切割 (Edge Cut) — JanusGraph, HBase
```
节点分配给服务器
跨服务器的边被复制或引用
问题: 遍历可能跨越网络
```

#### 顶点切割 (Vertex Cut) — 幂律图
```
大度节点 (超级节点) 被复制到多台服务器
边分配给不同服务器
适合社交网络图
```

### 分布式遍历
- **分片 BFS**: 每跳可能涉及网络通信
- **GAS 模型**: Gather → Apply → Scatter
  - Gather: 收集邻居信息
  - Apply: 更新节点状态
  - Scatter: 发送信号到边的另一端

## 7. 图神经网络 (GNN) 与图计算

### GNN 消息传递
```python
# 等价于图遍历 + 聚合
for layer in range(L):
    for node in graph:
        messages = aggregate(neighbor_embeddings)
        node.embedding = update(node.embedding, messages)
```

### 与图数据库的关系
- 图数据库存储特征和图结构
- 图引擎提供高效的邻居聚合
- 结果可写回数据库作为节点属性

## 从 mini-graph-db 到工业级的成长路径

### 第一阶段: 原型 (当前)
- 内存存储
- 简单索引
- 单线程
- 无持久化

### 第二阶段: 持久化
- WAL + Checkpoint
- 固定大小记录文件
- 启动恢复

### 第三阶段: 查询增强
- AST 基础查询解析器
- 索引辅助选择
- 简单 CBO

### 第四阶段: 并发
- 读写锁
- MVCC
- 乐观并发

### 第五阶段: 分布式
- 图分区
- 分布式遍历
- 共识协议

## 推荐阅读

### 理论基础
1. *Designing Data-Intensive Applications* (Martin Kleppmann, 2017) — 第 2-3 章: 存储引擎
2. *Graph Databases* (Robinson, Webber, Eifrem; O'Reilly, 2015) — 属性图模型
3. *Database Internals* (Alex Petrov, 2019) — 存储引擎与分布式系统

### 工业架构
4. *Neo4j Internals* (Neo4j Engineering Blog) — 原生图存储
5. *TigerGraph: A Native MPP Graph Database* (Deutsch et al., 2019) — 大规模并行图处理
6. *JanusGraph Architecture* (Linux Foundation) — 基于 BigTable 的分布式图

### 图查询语言
7. *Cypher: An Evolving Query Language* (Francis et al., SIGMOD 2018)
8. *Apache TinkerPop Documentation* — Gremlin 遍历机
9. *G-CORE: A Core for Future Graph Query Languages* (Angles et al., SIGMOD 2018)

### 图算法
10. *Graph Algorithms* (Needham & Hodler; O'Reilly, 2019) — 实用图算法指南
