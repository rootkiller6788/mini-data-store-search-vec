# Mini Data Store Search Vec（迷你数据存储搜索向量）

**从零开始、零依赖的 C 语言实现**，涵盖数据库内核、存储引擎、搜索系统和向量数据库核心概念。每个模块以教学级精度建模真实数据基础设施行为 — 从 B+Tree 缓冲池到 SQL 查询计划器、LSM 树压缩、倒排索引和 ANN 向量搜索。模块映射到 CMU 15-445/645、MIT 6.830、Stanford CS245 课程。

## 模块总览

| 模块 | 主题 | 参考标准 |
|--------|--------|----------------|
| [mini-db-kernel](mini-db-kernel/) | 缓冲池、B+Tree 索引、WAL、ARIES 恢复、锁管理器（2PL）、MVCC 基础 | CMU 15-445, Database Internals |
| [mini-relational-db](mini-relational-db/) | SQL 解析器、查询优化器（基于代价）、Volcano 迭代器执行器、JOIN 算法 | CMU 15-445, PostgreSQL |
| [mini-nosql-store](mini-nosql-store/) | K-V 存储、LSM 引擎、文档存储、列族存储、CRUD API | DynamoDB, Bigtable, MongoDB |
| [mini-graph-db](mini-graph-db/) | 属性图模型、邻接表、BFS/DFS 遍历、最短路径、PageRank | Neo4j, Gremlin |
| [mini-message-stream](mini-message-stream/) | Topic/Partition 模型、生产者/消费者、消费组重平衡、Offset 提交日志 | Kafka, Pulsar |
| [mini-search-engine](mini-search-engine/) | 倒排索引、分词/分析器、TF-IDF 评分、BM25 相关性、短语/布尔查询 | Lucene, Elasticsearch |
| [mini-vector-db](mini-vector-db/) | 向量嵌入、精确 KNN、近似 ANN（LSH、IVF、HNSW、PQ）、多样性距离 | Milvus, FAISS, Annoy |
| [mini-file-storage](mini-file-storage/) | LSM 树引擎、SSTable 格式、分级/通用压缩、WAL | LevelDB, RocksDB, ScyllaDB |

## 设计理念

- **零外部依赖** — 纯 C（C99/C11），仅使用 `libc` 和 `libm`
- **模块自包含** — 每个目录自带 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **存储引擎仿真** — 对数据库内部机制的教学级建模
- **理论到代码的映射** — 每个模块包含 `docs/` 目录，内有课程对齐说明
- **实用演示程序** — B+Tree 可视化、LSM 压缩仿真、ANN 索引构建等

## 构建方式

```bash
cd mini-db-kernel
make all    # 构建全部
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-data-store-search-vec/
├── mini-db-kernel/             # 数据库内核
├── mini-relational-db/         # 关系型数据库引擎
├── mini-nosql-store/           # NoSQL 存储
├── mini-graph-db/              # 图数据库
├── mini-message-stream/        # 消息流处理
├── mini-search-engine/         # 全文搜索
├── mini-vector-db/             # 向量相似度搜索
└── mini-file-storage/          # 文件存储（LSM/SSTable）
```

## 许可证

MIT
