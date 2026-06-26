# Course Alignment — mini-nosql-store

## 对应课程

本模块对接以下课程与知识体系：

### 数据库系统 (Database Systems)
| 知识点                        | mini-nosql 实现                    |
|-------------------------------|-----------------------------------|
| 存储引擎架构                  | LSM-tree engine (`lsm_engine`)    |
| 索引结构 (哈希/B-Tree/跳表)  | Skip list, Hash table            |
| 查询处理                      | Document find/range scan          |
| 事务与并发 (ACID vs BASE)     | 列族版本控制、墓碑删除            |
| BSON 序列化                   | Document BSON parsing            |
| 布隆过滤器                    | SSTable bloom filter              |

### 分布式系统 (Distributed Systems)
| 知识点                        | mini-nosql 实现                    |
|-------------------------------|-----------------------------------|
| 数据分区 (Tablet)             | Column-family tablet partition   |
| 一致性模型                    | MemTable + SSTable 层级          |
| 复制与一致性                  | 文档中的设计考量                  |
| 写前日志 (WAL)                | LSM-flush pipe (demos)           |

### 数据结构 (Data Structures)
| 知识点                        | mini-nosql 实现                    |
|-------------------------------|-----------------------------------|
| 跳表 (Skip List)              | MemTable + Redis ZSet            |
| 哈希表 (Hash Table)           | Redis Set/Hash, KV hash table    |
| 双向链表                      | Redis List                       |
| 布隆过滤器 (概率数据结构)     | SSTable bloom filter             |
| 有序集合 (Sorted Set)         | Redis ZSet (score-based)         |

### 系统编程
| 知识点                        | mini-nosql 实现                    |
|-------------------------------|-----------------------------------|
| 内存管理                      | calloc/free, 无泄漏设计          |
| 序列化/反序列化               | BSON parsing, SSTable format     |
| I/O 与文件组织                | LSM flush pipeline               |
| 模块化设计                    | include/src 分离                  |

---

## 学习路径建议

1. **入门**: `kv_store.h` → `kv_demo.c` (哈希表KV存储)
2. **核心**: `lsm_engine.h` → `lsm_demo.c` (LSM-tree存储引擎)
3. **文档**: `document_store.h` → `doc_store_demo.c` (MongoDB-like)
4. **分布式**: `col_family.h` (Bigtable列族)
5. **数据结构**: `redis_model.h` (Redis核心结构)
6. **深入**: `demos/mini-lsm-engine/README.md` (LSM详解)
7. **进阶**: `demos/mini-redis-structures/README.md` (Redis内部)

---

## 与工业系统的对照

| 工业系统     | mini-nosql 对应模块         | 学习要点                       |
|-------------|----------------------------|-------------------------------|
| DynamoDB    | `kv_store`                 | 键值抽象, 分区, TTL           |
| Bigtable    | `col_family`               | 列族, 版本, 分区 (Tablet)     |
| MongoDB     | `document_store`           | 文档模型, BSON, 二级索引       |
| Redis       | `redis_model`              | 多种数据结构, 内存效率         |
| LevelDB     | `lsm_engine`               | LSM-tree, 压缩, 布隆           |
| RocksDB     | `lsm_engine`               | 多层级压缩, 前缀压缩           |
| Cassandra   | `lsm_engine` + `col_family`| LSM + 宽列                     |
| HBase       | `col_family`               | 列族 + 区域服务器              |
