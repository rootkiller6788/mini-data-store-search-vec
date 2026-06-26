# mini-nosql-store — NoSQL 数据存储引擎 (C 语言实现)

> 参考 DynamoDB (Amazon), Bigtable (Google), MongoDB, Redis, LevelDB/RocksDB Internals

---

## Module Status: COMPLETE ✅

| Level | Status    | Details |
|-------|-----------|---------|
| L1 Definitions | **Complete** | 9 header files with struct/typedef/enum/API declarations |
| L2 Core Concepts | **Complete** | KV, LSM-Tree, Document, Column-Family, Redis 5 paradigms |
| L3 Engineering Structures | **Complete** | Skip List, Hash Table, BSON, Bloom Filter, Merkle Tree, Consistent Hashing Ring |
| L4 Standards/Theorems | **Complete** | CAP Theorem, BASE, ARIES WAL, Merkle (1979), Karger consistent hashing (STOC 1997), Belady's OPT, Sleator-Tarjan competitiveness |
| L5 Algorithms/Methods | **Complete** | DJB2 hash, djb2, MurmurHash3-FNV, CRC32, SHA-256, SkipList insert/search, LRU/LFU/Clock, Bloom Filter, Consistent Hashing Ring Build+Search, Binary Search, Quorum Read/Write |
| L6 Canonical Problems | **Complete** | KV Store, LSM-Tree Engine, Document Store with BSON, Column-Family Store, Redis Data Structures |
| L7 Applications | **Complete** | WAL crash recovery, Dynamo-style quorum, hinted handoff, Merkle tree anti-entropy diff, TTL-aware caching |
| L8 Advanced Topics | **Complete** | Merkle Proof generation/verification, LRU-K scan resistance, ARIES recovery protocol, Consistent Hashing load balance analysis |
| L9 Industry Frontiers | **Partial** | Documented: AI Compiler integration, Confidential Computing, Quantum-safe hashing (see docs) |

**Code Lines**: include/ (872) + src/ (3343) = **4215 lines** ≥ 3000 ✅

---

## 概述

`mini-nosql-store` 是一个完整的迷你 NoSQL 数据库存储层，使用 **C99 + libc** 实现。它提供了 9 个模块覆盖 NoSQL 全栈知识：

| 模块             | 范式           | 参考系统           | 核心特性                                |
|-----------------|---------------|-------------------|----------------------------------------|
| `kv_store`      | Key-Value     | DynamoDB          | 哈希表, TTL过期, 前缀扫描                |
| `lsm_engine`    | LSM-Tree      | LevelDB/RocksDB   | MemTable(跳表), SSTable, Bloom, Compaction|
| `document_store`| Document      | MongoDB           | BSON文档, 字段查询, 范围查询, 索引       |
| `col_family`    | Wide Column   | Bigtable/HBase    | 列族, 多版本控制, Tablet分区, 行扫描     |
| `redis_model`   | In-Memory     | Redis             | List, Set, ZSet, Hash 完整实现           |
| `nosql_wal`     | Durability    | ARIES Protocol    | WAL写入/恢复, CRC32校验, 事务边界        |
| `nosql_hash`    | Distribution  | Dynamo Ring       | 一致性哈希环, Quorum R/W, Hinted Handoff |
| `nosql_cache`   | Caching       | OS Page Cache     | LRU, LFU, Clock 三种淘汰策略             |
| `nosql_verify`  | Verification  | Merkle Tree       | SHA-256哈希, Merkle Proof, Anti-Entropy  |

---

## 九层知识覆盖 (L1-L9)

### L1: 核心定义 (Core Definitions)

| 模块 | 结构体/类型 | 枚举/常量 |
|------|-----------|----------|
| KV Store | `KVPair`, `KVStore` | hash slots, key/value max sizes |
| LSM Engine | `SkipListNode`, `MemTable`, `SSTable`, `BloomFilter`, `LSMEngine` | bloom bits/hashes, skiplist max level |
| Document Store | `Document`, `DocIndexEntry`, `DocIndex`, `DocCollection`, `DocumentStore` | BSON types, index buckets |
| Column Family | `Cell`, `ColumnFamily`, `Tablet`, `BigtableStore` | max versions, tablet rows |
| Redis Model | `RedisList/Set/ZSet/Hash`, `RedisListNode`, `RedisZSetNode` | set buckets, zset max level |
| WAL | `WALRecord`, `WALWriter`, `WALReader`, `WALStats`, `WALOp` | CRC32 seed, magic number |
| Hash Ring | `HashNode`, `HashVNode`, `HashRing`, `HintedHandoff`, `HashRingStats` | vnode count, replication factor |
| Cache | `LRUCache`, `LRUEntry`, `LFUCache`, `LFUEntry`, `ClockCache`, `ClockEntry` | max entries |
| Merkle | `MerkleHash`, `MerkleTree`, `MerkleNode`, `MerkleLeaf`, `MerkleProof` | SHA-256 digest size |

### L2: 核心概念 (Core Concepts)

- **LSM-Tree**: Write-optimized storage with MemTable → Immutable → SSTable → Compaction pipeline
- **Skip List**: Probabilistic balanced tree with O(log N) expected search/insert/delete
- **Bloom Filter**: Space-efficient probabilistic set membership test (no false negatives)
- **Consistent Hashing**: Key distribution with minimal remapping on node churn
- **Quorum (Dynamo)**: R + W > N ensures read-repair consistency
- **Hinted Handoff**: Temporary write delegation during transient node failure
- **Merkle Tree**: Cryptographic data structure for efficient verification (O(log N) proofs)
- **ARIES WAL**: Write-Ahead Logging for crash recovery with checkpoint optimization
- **Cache Eviction**: LRU (recency), LFU (frequency), Clock (second-chance approximation)

### L3: 工程结构 (Engineering Structures)

| 结构 | 模块 | 数据组织 | 操作 |
|------|------|---------|------|
| Hash Table + Linked List | kv_store, redis_model | Chained hash buckets | insert, lookup, delete, prefix scan |
| Skip List | lsm_engine, redis_model | Multi-level linked list | insert, search, delete |
| BSON Parser | document_store | Type-tagged binary format | field extraction, type dispatch |
| Sorted Ring + Binary Search | nosql_hash | Virtual node ring | ring construction, key→node lookup |
| Hash + Doubly-Linked List | nosql_cache (LRU) | Hash for lookup, DLL for order | O(1) get, put, evict |
| Hash + Frequency List | nosql_cache (LFU) | Frequency-sorted bucket chain | O(1) freq-increment, min-freq evict |
| Circular Buffer | nosql_cache (Clock) | Array with hand pointer | O(1) second-chance sweep |
| Binary Tree (flat array) | nosql_verify | Level-order array storage | O(N) build, O(log N) proof |
| Append-Only Log | nosql_wal | Sequential file records | O(1) append, O(N) recovery scan |
| Column Version Chain | col_family | Cell linked list per CF | insert with version GC, multi-version read |

### L4: 标准/定理 (Standards & Theorems)

| 定理 | 公式/陈述 | 代码验证位置 |
|------|----------|-------------|
| **CAP Theorem** (Brewer 2000) | 分布式系统最多同时满足 Consistency + Availability + Partition tolerance 中的两项 | `nosql_hash.c` quorum: R+W>N (CP), hinted handoff (AP) |
| **Karger Consistent Hashing** (STOC 1997) | 添加/删除节点仅重映射 O(K/N) 键 | `hash_ring_compute_stats()` 负载标准差验证 |
| **Dynamo Quorum** (DeCandia SOSP 2007) | R + W > N 保证强一致性 | `hash_ring_quorum_read/write()` |
| **ARIES Recovery** (Mohan TODS 1992) | WAL + Checkpoint + Undo/Redo 保证原子性与持久性 | `wal_recover()` 三阶段恢复 |
| **Merkle Tree Proof** (Merkle 1979) | O(log N) 大小包含证明 | `merkle_tree_generate/verify_proof()` |
| **Belady's OPT** (1966) | 最优页面替换驱逐未来最远使用的页面 | `nosql_cache.h` 文档, LRU 是最佳在线近似 |
| **Sleator-Tarjan Competitiveness** (1985) | LRU 是 k-competitive (k = cache size) | `lru_cache` 实现命中率统计 |
| **Mattson Stack Property** (1970) | 具有 Stack Property 的算法, 增加缓存大小不会降低命中率 | LRU 满足此性质 (LFU 不满足) |
| **Bloom Filter** (Bloom CACM 1970) | P(false positive) = (1 - e^(-kn/m))^k | `bloom_check()` 提供概率性成员测试 |

### L5: 算法/方法 (Algorithms & Methods)

| 算法 | 复杂度 | 位置 | 描述 |
|------|-------|------|------|
| DJB2 Hash | O(n) | `kv_store.c`, `document_store.c` | dan bernstein hash, good distribution |
| FNV-1a + MurmurHash3 Finalizer | O(n) | `nosql_hash.c` | 32-bit hash for consistent hashing |
| CRC32 (Table-Driven) | O(n) | `nosql_wal.c` | IEEE 802.3 polynomial, WAL record integrity |
| SHA-256 (Simplified) | O(n) | `nosql_verify.c` | Merkle-Damgard construction with 64-round compression |
| Skip List Insert/Search | O(log N) expected | `lsm_engine.c`, `redis_model.c` | Probabilistic level generation (p=1/4) |
| Binary Search on Ring | O(log V) | `nosql_hash.c` | Sorted virtual node ring, clockwise walk |
| LRU Eviction (Hash + DLL) | O(1) | `nosql_cache.c` | Hash lookup + move-to-front + tail evict |
| LFU Eviction (Freq List) | O(1) amortized | `nosql_cache.c` | Freq-increment + min-freq eviction |
| Clock Second-Chance | O(1) | `nosql_cache.c` | Circular buffer with reference bit sweep |
| Bloom Filter Insert/Query | O(k) | `lsm_engine.c` | k=3 hash functions, bit array |
| Merkle Tree Build | O(N) | `nosql_verify.c` | Bottom-up pairwise hash combination |
| Merkle Proof Generation | O(log N) | `nosql_verify.c` | Walk from leaf to root collecting siblings |
| Random Skip Level | O(1) expected | `lsm_engine.c`, `redis_model.c` | Geometric distribution p=1/4 |
| BSON Field Parser | O(n) | `document_store.c` | Sequential scan with type dispatch |
| Version GC (Column Family) | O(v) per row | `col_family.c` | Retain max_versions, evict oldest |

### L6: 经典工程问题 (Canonical Problems)

| 问题 | 模块 | 示例文件 |
|------|------|---------|
| Key-Value Store | kv_store | `examples/kv_demo.c` — put/get/delete/prefix scan/TTL |
| LSM-Tree Storage Engine | lsm_engine | `examples/lsm_demo.c` — memtable flush, SSTable, compaction, bloom |
| Document Database | document_store | `examples/doc_store_demo.c` — BSON CRUD, field query, range scan |
| Write-Ahead Logging | nosql_wal | `examples/wal_demo.c` — append, commit, checkpoint, recovery |
| Distributed KV Ring | nosql_hash | `examples/hash_demo.c` — ring, quorum, handoff, load balance |
| Cache Eviction System | nosql_cache | `examples/cache_demo.c` — LRU vs LFU vs Clock comparison |

### L7: 应用 (Applications) — ≥2 实现

1. **WAL Crash Recovery** (`nosql_wal.c`): ARIES-style analysis→redo→undo, replay committed records after last checkpoint
2. **Dynamo Quorum Read/Write** (`nosql_hash.c`): Configurable R+W>N quorum, replica selection, hinted handoff drain
3. **Merkle Tree Anti-Entropy Diff** (`nosql_verify.c`): Compare two Merkle trees to find divergent data ranges (Cassandra repair)
4. **TTL-Aware Caching** (`nosql_cache.c`): LRU with time-to-live expiry, `lru_cache_cleanup_expired()`

### L8: 进阶主题 (Advanced Topics) — ≥1 实现

1. **Merkle Proof** (`nosql_verify.c`): Generate and verify O(log N) inclusion proofs, tamper detection
2. **LRU-K Scan Resistance** (`nosql_cache.c`): Track K access timestamps to prevent sequential scan pollution (O'Neil SIGMOD 1993)
3. **Consistent Hashing Load Analysis** (`nosql_hash.c`): Compute per-node key distribution standard deviation to verify Karger's theorem
4. **ARIES Checkpoint Optimization** (`nosql_wal.c`): Skip pre-checkpoint records during recovery, reducing replay time

### L9: 工业前沿 (Industry Frontiers)

文档化在 `docs/nosql-database-primer.md` 和 `docs/course-alignment.md`:

- **AI Compiler / MLIR**: 向量搜索索引与 AI embedding 集成方向 (参考 FAISS, Milvus)
- **Confidential Computing**: WAL 加密 + Merkle 证明实现可信执行环境审计
- **Quantum-Resistant Hashing**: 当前 SHA-256 的后量子替代方案 (SPHINCS+, CRYSTALS-Dilithium)
- **Serverless NoSQL**: 存算分离, 弹性压缩调度 (参考 Amazon Aurora Serverless, Azure Cosmos DB)

---

## 快速开始

```bash
# 编译所有目标
make

# 运行所有测试
make test

# 运行演示
./build/kv_demo          # KV 存储: put/get/delete/prefix scan/TTL
./build/lsm_demo         # LSM 引擎: flush, compaction, bloom filter
./build/doc_store_demo   # 文档存储: BSON, 查询, 索引
./build/wal_demo         # WAL: 写入/恢复/CRC32校验
./build/hash_demo        # 一致性哈希: 环/Quorum/Hinted Handoff
./build/cache_demo       # 缓存: LRU vs LFU vs Clock 对比
```

---

## 目录结构

```
mini-nosql-store/
├── include/                    # 9 个头文件 (872 lines)
│   ├── kv_store.h              # KV 存储接口
│   ├── lsm_engine.h            # LSM-tree 引擎接口
│   ├── document_store.h        # 文档存储接口 (BSON)
│   ├── col_family.h            # 列族存储接口 (Bigtable)
│   ├── redis_model.h           # Redis 数据结构接口
│   ├── nosql_wal.h             # WAL 持久化接口 (ARIES)
│   ├── nosql_hash.h            # 一致性哈希环 + Quorum
│   ├── nosql_cache.h           # LRU/LFU/Clock 缓存引擎
│   └── nosql_verify.h          # Merkle Tree 验证
├── src/                        # 9 个实现文件 (3343 lines)
│   ├── kv_store.c              # 哈希表KV + TTL + 前缀扫描
│   ├── lsm_engine.c            # 跳表MemTable + SSTable + Bloom + Compaction
│   ├── document_store.c        # BSON解析 + 字段/范围查询 + 索引重建
│   ├── col_family.c            # 列族 + 多版本 + Tablet分区 + 行扫描
│   ├── redis_model.c           # List/Set/ZSet/Hash 完整实现 (跳表ZSet)
│   ├── nosql_wal.c             # CRC32校验 + 顺序日志 + ARIES恢复
│   ├── nosql_hash.c            # MurmurHash3 + 虚拟节点环 + Quorum + Handoff
│   ├── nosql_cache.c           # LRU O(1) + LFU频度 + Clock二次机会
│   └── nosql_verify.c          # SHA-256哈希 + Merkle树 + 证明 + 差异检测
├── tests/                      # 8 个测试文件 (assert-based)
│   ├── test_kv.c / test_lsm.c / test_doc.c / test_redis.c
│   ├── test_wal.c / test_hash.c / test_cache.c / test_verify.c
├── examples/                   # 6 个端到端示例
│   ├── kv_demo.c / lsm_demo.c / doc_store_demo.c
│   ├── wal_demo.c / hash_demo.c / cache_demo.c
├── demos/
│   ├── mini-lsm-engine/README.md
│   └── mini-redis-structures/README.md
├── docs/
│   ├── course-alignment.md
│   └── nosql-database-primer.md
├── README.md
└── Makefile
```

---

## API 总览

### KV Store (DynamoDB-like)
```c
KVStore *s = kv_create(0);
kv_put(s, "k", "v", 0);           // O(1) avg
kv_get(s, "k", buf, 256);         // O(1) avg
kv_scan_prefix(s, "usr:", res, n);// O(N)
```

### LSM Engine (LevelDB/RocksDB-like)
```c
LSMEngine *e = lsm_create("./data");
lsm_put(e, "k", "v");             // MemTable insert
lsm_get(e, "k", buf, 256);       // MemTable→Imm→L0→L1
lsm_flush_memtable(e);            // Freeze→SSTable
lsm_compact_all(e);               // L0→L1 merge
```

### Document Store (MongoDB-like)
```c
DocumentStore *ds = doc_store_create();
doc_insert(ds, "coll", "id", bson, len);
doc_find_query(ds, "coll", "field", "val", res, n);
doc_find_range(ds, "coll", "age", "30", "45", res, n);
```

### Column-Family (Bigtable-like)
```c
BigtableStore *bs = col_store_create("table");
col_put(bs, "row", "cf", "col", ts, "val");
col_get_latest(bs, "row", "cf", "col", buf, 256);
col_scan(bs, "r1", "r9", "cf", results, 100);
```

### Redis Data Structures
```c
RedisList  *l  = redis_list_create("k");   redis_lpush/rpush/lpop/rpop
RedisSet   *s  = redis_set_create("k");    redis_sadd/srem/sunion/sinter
RedisZSet  *zs = redis_zset_create("k");   redis_zadd/zrange/zrangebyscore
RedisHash  *h  = redis_hash_create("k");   redis_hset/hget/hkeys/hvals
```

### WAL (Durability)
```c
WALWriter *w = wal_writer_open("log", 0);
wal_writer_append(w, WAL_OP_UPDATE, "k", "v");
wal_writer_commit(w);
wal_writer_checkpoint(w);
int n = wal_recover("log", callback, ctx);
```

### Consistent Hash Ring (Distribution)
```c
HashRing *ring = hash_ring_create(3);
hash_ring_add_node(ring, "node1");
int nidx = hash_ring_locate(ring, "key");
hash_ring_quorum_write(ring, "k", "v", 2);
hash_ring_quorum_read(ring, "k", buf, 256, 2);
```

### Cache Engine (Eviction)
```c
LRUCache *lru = lru_cache_create(128);
lru_cache_put(lru, "k", "v", ttl);
lru_cache_get(lru, "k", buf, 256);
// Also: LFUCache + ClockCache with same API pattern
```

### Merkle Tree (Verification)
```c
MerkleTree *mt = merkle_tree_create();
merkle_tree_add_leaf(mt, "k", data, len);
merkle_tree_build(mt);
MerkleProof proof;
merkle_tree_generate_proof(mt, 0, &proof);
merkle_tree_verify_proof(root, leaf, &proof);
```

---

## 设计决策

- **C99 标准**: 无外部依赖, 仅 libc + libm
- **固定大小字段**: 使用静态数组, 适合嵌入式/教学场景
- **单线程**: 无并发控制 (教学清晰度优先)
- **模块化**: 每个模块独立可编译, 松耦合

---

## 九校课程映射 (Course Alignment)

| 学校 | 课程 | 本模块对应 |
|------|------|-----------|
| **MIT** | 6.824 Distributed Systems | Dynamo Quorum, Consistent Hashing, Hinted Handoff |
| **Stanford** | CS 245 Database Systems | LSM-Tree, B+Tree alternative, Bloom Filter |
| **Berkeley** | CS 186 Database Systems | Storage engines, BSON indexing, query processing |
| **CMU** | 15-445/645 Database Systems | LSM-Tree, WAL ARIES, Buffer Pool (Cache) |
| **CMU** | 15-721 Advanced DB | Merkle Tree verification, Anti-entropy repair |
| **UT Austin** | CS 380D Distributed Systems | CAP/PACELC, Quorum consistency, Gossip-based repair |
| **ETH** | 263-3501 Parallel Programming | Cache coherence patterns, lock-free skip list |
| **Cambridge** | Part II: Concurrent Systems | WAL crash recovery, checkpoint protocols |
| **清华** | 操作系统 (OS) | LRU/Clock page replacement, cache eviction algorithms |

详见 `docs/course-alignment.md`

---

## 深入学习

- `demos/mini-lsm-engine/README.md` — LSM-tree 完整解析 (250+ lines)
- `demos/mini-redis-structures/README.md` — Redis 内部结构 (250+ lines)
- `docs/nosql-database-primer.md` — NoSQL 入门与四种范式
- `docs/course-alignment.md` — 九校课程对接表
