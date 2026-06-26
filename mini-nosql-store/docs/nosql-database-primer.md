# NoSQL Database Primer — mini-nosql-store

> 一份快速上手指南：理解 NoSQL 的核心概念与四种主要范式

---

## 什么是 NoSQL？

NoSQL (Not Only SQL) 是一类非关系型数据库管理系统的总称，设计目标包括：

- 水平扩展 (scale-out) 优于垂直扩展 (scale-up)
- 灵活的数据模型 (schema-less/schema-flexible)
- 高性能读写 (高吞吐、低延迟)
- 最终一致性模型 (BASE 替代 ACID)

### SQL vs NoSQL 对比

| 维度         | SQL (RDBMS)              | NoSQL                     |
|-------------|--------------------------|---------------------------|
| 数据模型     | 表 + 行 + 列 (固定 schema)| 文档/键值/列族/图 (灵活)  |
| 扩展方式     | 垂直 (更强的单机)         | 水平 (更多机器)           |
| 事务         | ACID (强一致)            | BASE (最终一致)           |
| 查询语言     | SQL (标准化)             | 自定义 API / 查询DSL      |
| 典型产品     | MySQL, PostgreSQL        | MongoDB, Redis, Cassandra |

---

## 四种 NoSQL 范式

### 1. 键值存储 (Key-Value Store)

最简单的 NoSQL 形式。每个数据项通过唯一键访问。

**代表**: DynamoDB, Redis, Riak, etcd

**数据模型**:
```
"user:1001" → {"name": "Alice", "age": 28}
"session:abc" → "active"
```

**优点**:
- 极致性能 (O(1) 查找)
- 简单可靠
- 天然支持分区 (hash-based sharding)

**缺点**:
- 无法按值查询 (只能按键)
- 不支持复杂关系

**mini-nosql 实现**: `kv_store.h/c` — 哈希表 + TTL 过期 + 前缀扫描

### 2. 文档存储 (Document Store)

存储和查询半结构化文档 (JSON/BSON/XML)。

**代表**: MongoDB, CouchDB, Firestore

**数据模型**:
```json
{
  "_id": "u1001",
  "name": "Alice",
  "age": 28,
  "address": {
    "city": "Beijing",
    "zip": "100000"
  },
  "tags": ["developer", "gamer"]
}
```

**优点**:
- 灵活的数据模型 (可嵌套)
- 按字段查询和索引
- 自然的对象映射 (ORM 友好)

**缺点**:
- 文档间 JOIN 低效
- 嵌套文档更新粒度粗

**mini-nosql 实现**: `document_store.h/c` — BSON 解析, 字段查询, 范围查询, 索引

### 3. 列族存储 (Column-Family Store)

数据按行键、列族、列限定符、时间戳的多维结构组织。

**代表**: Bigtable, HBase, Cassandra

**数据模型**:
```
Row Key: "user:1001"
  ColumnFamily: "profile"
    "name" @t1 → "Alice"
    "email" @t2 → "alice@example.com"
    "email" @t1 → "alice@old.com"
  ColumnFamily: "stats"
    "login_count" @t3 → "42"
```

**优点**:
- 稀疏列支持 (不同行可以有不同列)
- 多版本 (时间戳版本控制)
- 高效的行级扫描

**缺点**:
- 设计复杂度高
- 跨行查询困难

**mini-nosql 实现**: `col_family.h/c` — 列族, 版本化, Tablet 分区, 行范围扫描

### 4. 图数据库 (Graph Database)

(本实现不涉及, 留作扩展)

**代表**: Neo4j, Amazon Neptune, JanusGraph

---

## 关键概念深入

### CAP 定理

分布式数据系统只能在以下三者中同时满足两项：

```
       C (一致性)
       /\
      /  \
     /    \
    /      \
   /________\
  A (可用性)  P (分区容忍)
```

| 选择   | 典型系统        | 行为                           |
|--------|----------------|-------------------------------|
| CP     | HBase, Bigtable| 分区时牺牲可用性, 保持一致性   |
| AP     | Cassandra, Dynamo | 分区时牺牲一致性, 保持可用性 |
| CA     | 单机 RDBMS     | 仅单机可行, 不支持分区容忍    |

### BASE 模型

替代 ACID 的分布式一致性模型：

- **B**asically **A**vailable: 基本可用 (允许部分不可用)
- **S**oft state: 软状态 (允许中间状态)
- **E**ventually consistent: 最终一致 (经过一段时间后一致)

### LSM-Tree 存储模型

几乎所有现代 NoSQL 数据库的底层存储引擎：

```
写入 → MemTable (内存排序) → SSTable (磁盘有序) → Compaction (合并清理)
```

详细信息参见 `demos/mini-lsm-engine/README.md`

---

## mini-nosql-store 模块总览

```
mini-nosql-store/
├── include/
│   ├── kv_store.h          # 键值存储接口
│   ├── lsm_engine.h        # LSM-tree 存储引擎
│   ├── document_store.h    # 文档存储 (BSON)
│   ├── col_family.h        # 列族存储 (Bigtable)
│   └── redis_model.h       # Redis 数据结构
├── src/
│   ├── kv_store.c          # 哈希表实现 + TTL + 前缀扫描
│   ├── lsm_engine.c        # MemTable (跳表) + SSTable + Compaction
│   ├── document_store.c    # BSON 解析 + 字段查询 + 索引
│   ├── col_family.c        # 列族 + 版本控制 + Tablet 分区
│   └── redis_model.c       # List/Set/ZSet/Hash 完整实现
├── examples/
│   ├── kv_demo.c           # KV 操作演示
│   ├── lsm_demo.c          # LSM flush/compaction 演示
│   └── doc_store_demo.c    # 文档 CRUD 演示
├── demos/
│   ├── mini-lsm-engine/    # LSM-tree 深度解析 (250+ 行)
│   └── mini-redis-structures/ # Redis 结构深度解析 (250+ 行)
├── docs/
│   ├── course-alignment.md  # 课程对接
│   └── nosql-database-primer.md  # 本文
├── README.md
└── Makefile
```

---

## 快速开始

```bash
make
./build/kv_demo
./build/lsm_demo
./build/doc_store_demo
```

---

## 参考资料

- **Dynamo**: Amazon's Dynamo Paper (2007)
- **Bigtable**: Google's Bigtable Paper (2006)
- **LevelDB**: Google, Jeff Dean & Sanjay Ghemawat
- **Redis Internals**: Redis Documentation & Source Code
- **Designing Data-Intensive Applications**: Martin Kleppmann
- **Database Internals**: Alex Petrov
