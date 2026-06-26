# mini-nosql-store — NoSQL 存储 (C 语言实现)

> 参考 DynamoDB (Amazon), Bigtable (Google), MongoDB, Redis Internals

---

## 概述

`mini-nosql-store` 是一个完整的迷你 NoSQL 数据库存储层，使用 **C99 + libc** 实现。它提供了四种 NoSQL 范式的参考实现：

| 模块            | 范式         | 参考系统      | 核心特性                           |
|----------------|-------------|--------------|-----------------------------------|
| `kv_store`     | Key-Value   | DynamoDB     | 哈希表, TTL过期, 前缀扫描           |
| `lsm_engine`   | LSM-Tree    | LevelDB/RocksDB | MemTable(跳表), SSTable, Compaction |
| `document_store`| Document    | MongoDB      | BSON文档, 字段查询, 索引            |
| `col_family`   | Wide Column | Bigtable     | 列族, 版本控制, Tablet分区          |
| `redis_model`  | In-Memory   | Redis        | List, Set, ZSet, Hash 完整实现      |

---

## 快速开始

```bash
# 编译所有目标
make

# 运行演示
./build/kv_demo          # KV 存储: put/get/delete/prefix scan
./build/lsm_demo         # LSM 引擎: flush, compaction, bloom filter
./build/doc_store_demo   # 文档存储: BSON, 查询, 索引
```

---

## 目录结构

```
mini-nosql-store/
├── include/
│   ├── kv_store.h          # KV 存储接口
│   ├── lsm_engine.h        # LSM-tree 引擎接口
│   ├── document_store.h    # 文档存储接口
│   ├── col_family.h        # 列族存储接口
│   └── redis_model.h       # Redis 数据结构接口
├── src/
│   ├── kv_store.c          # 哈希表KV实现
│   ├── lsm_engine.c        # LSM-tree完整实现
│   ├── document_store.c    # 文档存储实现
│   ├── col_family.c        # 列族存储实现
│   └── redis_model.c       # Redis数据结构实现
├── examples/
│   ├── kv_demo.c           # KV 演示
│   ├── lsm_demo.c          # LSM 引擎演示
│   └── doc_store_demo.c    # 文档存储演示
├── demos/
│   ├── mini-lsm-engine/README.md         # LSM-tree 深度解析
│   └── mini-redis-structures/README.md   # Redis 结构详解
├── docs/
│   ├── course-alignment.md               # 课程对接
│   └── nosql-database-primer.md          # NoSQL 入门指南
├── README.md
└── Makefile
```

---

## API 概览

### KV Store (DynamoDB-like)

```c
KVStore *store = kv_create(0);
kv_put(store, "key1", "value1", 0);       // 写入, TTL=0 永不过期
kv_put(store, "temp", "val", time(NULL)+60); // 60秒过期
kv_get(store, "key1", buf, sizeof(buf));  // 读取
kv_delete(store, "key1");                 // 删除
kv_scan_prefix(store, "user:", results, 16); // 前缀扫描
```

### LSM Engine (LevelDB-like)

```c
LSMEngine *engine = lsm_create("./data");
lsm_put(engine, "key:001", "value_001");  // 写入MemTable
lsm_get(engine, "key:001", buf, 256);     // 查找: MemTable→Immutable→L0→L1
lsm_flush_memtable(engine);               // 冻结→SSTable
lsm_compact_all(engine);                  // Level-0→Level-1合并
```

### Document Store (MongoDB-like)

```c
DocumentStore *store = doc_store_create();
doc_insert(store, "users", "u1", bson_data, len);
doc_find(store, "users", "u1");                             // 按ID查找
doc_find_query(store, "users", "city", "Beijing", res, 16); // 字段查询
doc_find_range(store, "users", "age", "30", "45", res, 16); // 范围查询
doc_create_index(store, "users", "name");                    // 创建索引
```

### Column-Family Store (Bigtable-like)

```c
BigtableStore *store = col_store_create("mytable");
col_put(store, "row1", "profile", "name", ts, "Alice");
col_get_latest(store, "row1", "profile", "name", buf, 256);
col_scan(store, "row1", "row5", "profile", results, 100);
```

### Redis Data Structures

```c
RedisList  *list  = redis_list_create("mylist");
RedisSet   *set   = redis_set_create("myset");
RedisZSet  *zset  = redis_zset_create("leaderboard");
RedisHash  *hash  = redis_hash_create("user:1");

redis_lpush(list, "hello");
redis_sadd(set, "member1");
redis_zadd(zset, 100.0, "player1");
redis_hset(hash, "name", "Alice");
```

---

## 设计决策

- **C99 标准**: 无外部依赖, 仅 libc + libm
- **固定大小字段**: 使用静态数组, 适合嵌入式/教学场景
- **无持久化**: 纯内存 (LSM SSTable 同理)
- **单线程**: 无并发控制
- **模块化**: 每个模块独立可编译

---

## 深入学习

- `demos/mini-lsm-engine/README.md` — LSM-tree 完整解析 (250+ lines)
- `demos/mini-redis-structures/README.md` — Redis 内部结构 (250+ lines)
- `docs/nosql-database-primer.md` — NoSQL 入门
- `docs/course-alignment.md` — 课程对接
