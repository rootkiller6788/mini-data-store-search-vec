# Redis-like Data Structures — mini-nosql-store

> 参考 Redis Internals — 在内存中使用 C 模拟 Redis 核心数据结构

---

## 目录

1. [概述](#概述)
2. [List — 双向链表](#list--双向链表)
3. [Set — 哈希集合](#set--哈希集合)
4. [Sorted Set — 跳表有序集合](#sorted-set--跳表有序集合)
5. [Hash — 字段值映射](#hash--字段值映射)
6. [跳表 (Skip List) 详解](#跳表-skip-list-详解)
7. [与 Redis 命令的对应](#与-redis-命令的对应)
8. [复杂度分析](#复杂度分析)
9. [内存布局](#内存布局)
10. [使用示例](#使用示例)
11. [扩展方向](#扩展方向)

---

## 概述

Redis 以其丰富的数据结构著称，支持 List、Set、Sorted Set、Hash、String、Bitmaps、HyperLogLog、Streams 等。本实现提供了其中四种核心数据结构的 C 语言模拟：

- **List**: 双向链表，支持两端操作
- **Set**: 哈希表集合，支持集合运算
- **Sorted Set**: 跳表实现，按分值排序
- **Hash**: 哈希映射，字段→值

这些数据结构完全在内存中运行，不涉及网络通信或持久化，是理解 Redis 内部机制的绝佳起点。

### 设计原则

1. **零外部依赖**: 仅使用 C99 标准库 (libc)
2. **固定大小字段**: 使用静态数组而非动态分配 (模仿嵌入式场景)
3. **清晰 API**: 函数命名与 Redis 命令一致 (lpush, sadd, zadd, hset)
4. **模块化**: 每个数据结构独立可用
5. **线程不安全**: 单线程操作 (与 Redis 6.0 之前一致)

---

## List — 双向链表

### 结构定义

```c
typedef struct redis_list_node_t {
    char  value[REDIS_MAX_VALUE_LEN];        // 256 bytes
    struct redis_list_node_t *prev;
    struct redis_list_node_t *next;
} RedisListNode;

typedef struct redis_list_t {
    RedisListNode *head;
    RedisListNode *tail;
    int            length;
    char           key[REDIS_MAX_KEY_LEN];    // 64 bytes
} RedisList;
```

### 实现细节

Redis List 的实际实现是 **quicklist**（`ziplist` + `linkedlist` 的混合），在存储少量短元素时使用 `ziplist`（紧凑编码），在元素较多时使用 `linkedlist`。本实现简化为纯双向链表。

### 支持的操作

| 操作                | 描述                              | 复杂度  |
|---------------------|-----------------------------------|---------|
| `redis_lpush`       | 左侧插入                          | O(1)    |
| `redis_rpush`       | 右侧插入                          | O(1)    |
| `redis_lpop`        | 左侧弹出                          | O(1)    |
| `redis_rpop`        | 右侧弹出                          | O(1)    |
| `redis_lrange`      | 范围获取 (start..stop)            | O(n)    |
| `redis_llen`        | 获取长度                          | O(1)    |
| `redis_lindex`      | 按索引获取                        | O(n)    |

### 内部工作原理

```
LPUSH mylist "C" → LPUSH mylist "B" → LPUSH mylist "A"

   HEAD → [A] ⇄ [B] ⇄ [C] ← TAIL

LRANGE mylist 0 -1  →  [A, B, C]
LPOP mylist          →  A
RPOP mylist          →  C
```

索引支持负数，遵循 Redis 惯例：
- `0` = 第一个元素
- `-1` = 最后一个元素
- `-2` = 倒数第二个元素

---

## Set — 哈希集合

### 结构定义

```c
typedef struct redis_set_entry_t {
    char  value[REDIS_MAX_VALUE_LEN];
    struct redis_set_entry_t *next;
} RedisSetEntry;

typedef struct redis_set_t {
    RedisSetEntry *buckets[REDIS_SET_BUCKETS];  // 128 buckets
    int            count;
    char           key[REDIS_MAX_KEY_LEN];
} RedisSet;
```

### 实现细节

Redis 的 Set 根据内容自动选择编码：
- **intset**：所有成员都是整数且数量较少时
- **hashtable**：通用情况

本实现统一使用 **链地址法哈希表**，使用 DJB2 哈希函数。

### 支持的操作

| 操作                | 描述                              | 复杂度     |
|---------------------|-----------------------------------|-----------|
| `redis_sadd`        | 添加元素                          | O(1) 平均  |
| `redis_srem`        | 删除元素                          | O(1) 平均  |
| `redis_sismember`   | 检查成员                          | O(1) 平均  |
| `redis_smembers`    | 获取所有成员                      | O(n)      |
| `redis_scard`       | 获取基数                          | O(1)      |
| `redis_sunion`      | 并集                              | O(n+m)    |
| `redis_sinter`      | 交集                              | O(n*m)    |

### 哈希冲突处理

使用链地址法 (separate chaining) 解决冲突。每个桶存储单向链表。平均情况下负载因子 < 1 时，操作复杂度为 O(1)。

```
buckets[0]  →  [value_a] → [value_b] → NULL   (hash(value_a) ≡ hash(value_b) ≡ 0)
buckets[1]  →  NULL
buckets[2]  →  [value_c] → NULL
...
```

---

## Sorted Set — 跳表有序集合

### 结构定义

```c
typedef struct redis_zset_node_t {
    char    member[REDIS_MAX_VALUE_LEN];       // 256 bytes
    double  score;                              // 排序分值
    struct redis_zset_node_t *forward[12];      // 多层前向指针
    int     span[12];                           // 跨度 (可选, 为 rank 优化)
    struct redis_zset_node_t *backward;         // 后向指针
} RedisZSetNode;

typedef struct redis_zset_t {
    RedisZSetNode *header;
    RedisZSetNode *tail;
    unsigned long  length;
    int            level;
    char           key[REDIS_MAX_KEY_LEN];
} RedisZSet;
```

### 为什么用跳表？

Redis 选择跳表而非平衡树来实现 Sorted Set，原因：

1. **实现简单**: 跳表代码量约为红黑树的 1/3
2. **有序遍历**: 天然支持，无需维护迭代器栈
3. **范围查询高效**: 直接前向遍历
4. **并发友好**: 更易于实现无锁版本
5. **调试容易**: 层级结构直观可读

### 支持的操作

| 操作                  | 描述                              | 复杂度     |
|-----------------------|-----------------------------------|-----------|
| `redis_zadd`          | 添加成员 (含分值)                 | O(log n)  |
| `redis_zrem`          | 删除成员                          | O(log n)  |
| `redis_zscore`        | 获取成员分值                      | O(n)      |
| `redis_zrange`        | 按索引范围获取 (start..stop)       | O(log n+m)|
| `redis_zrangebyscore` | 按分值范围获取                    | O(log n+m)|
| `redis_zrank`         | 获取成员排名                      | O(n)      |
| `redis_zcard`         | 获取基数                          | O(1)      |

---

## Hash — 字段值映射

### 结构定义

```c
typedef struct redis_hash_entry_t {
    char  field[REDIS_MAX_FIELD_LEN];          // 64 bytes
    char  value[REDIS_MAX_VALUE_LEN];          // 256 bytes
    struct redis_hash_entry_t *next;
} RedisHashEntry;

typedef struct redis_hash_t {
    RedisHashEntry *buckets[REDIS_SET_BUCKETS]; // 128 buckets
    int             count;
    char            key[REDIS_MAX_KEY_LEN];
} RedisHash;
```

### 实现细节

Redis 的 Hash 也有双编码：
- **ziplist**：字段和值都较短且数量少时
- **hashtable**：通用情况

本实现使用链地址法哈希表，与 Set 一致的设计。每个桶存储冲突的 HashEntry 链表。

### 支持的操作

| 操作                | 描述                              | 复杂度     |
|---------------------|-----------------------------------|-----------|
| `redis_hset`        | 设置字段值                        | O(1) 平均  |
| `redis_hget`        | 获取字段值                        | O(1) 平均  |
| `redis_hdel`        | 删除字段                          | O(1) 平均  |
| `redis_hexists`     | 检查字段存在                      | O(1) 平均  |
| `redis_hkeys`       | 获取所有字段                      | O(n)      |
| `redis_hvals`       | 获取所有值                        | O(n)      |
| `redis_hlen`        | 获取字段数                        | O(1)      |
| `redis_hgetall`     | 获取所有字段和值                  | O(n)      |

---

## 跳表 (Skip List) 详解

跳表本质上是在有序链表基础上添加多层索引，实现 O(log n) 的查找复杂度。

### 层数决定机制

每个新节点通过抛硬币（概率 P）决定层数：

```c
static int zset_random_level(void) {
    int lvl = 0;
    while (lvl < REDIS_ZSET_MAXLVL - 1 && (rand() % 4) == 0)
        lvl++;
    return lvl;
}
```

- P = 1/4: Redis 使用的晋升概率
- MAX_LEVEL = 12: 在 P=1/4 下，支持约 1600 万条目

### 层数分布 (期望)

| 层数   | 概率           | 期望节点数 (N=1M) |
|--------|----------------|-------------------|
| ≥ 0    | 100%           | 1,000,000        |
| ≥ 1    | 25%            | 250,000          |
| ≥ 2    | 6.25%          | 62,500           |
| ≥ 3    | 1.56%          | 15,625           |
| ≥ 4    | 0.39%          | 3,906            |
| ≥ 5    | 0.098%         | 977              |

### 查找算法

```
zrangebyscore: 查找分值在 [min, max] 的元素

1. 从头节点最高层开始
2. 当同层下一个节点的分值 ≤ max:
     a. 如果分值 ≥ min: 收集该节点
     b. 前移到下一个节点
3. 如果下一个节点分值 > max 或 NULL:
     a. 下降一层
     b. 重复步骤 2
4. 当降到第 0 层且无法前移时停止
```

### 插入算法

```
zadd(score, member):

1. 从头节点最高层开始, 记录每层的 update[i] (前驱节点)
2. 找到插入位置 (按分值排序, 同分值按字典序)
3. 随机生成新节点的层数
4. 如果新层数 > 当前最高层:
     update[i] = header (高层的前驱置为头节点)
5. 插入节点:
     node.forward[i] = update[i].forward[i]
     update[i].forward[i] = node
6. 设置后向指针
```

---

## 与 Redis 命令的对应

| mini-nosql 函数          | Redis 命令        | 说明                     |
|--------------------------|-------------------|-------------------------|
| `redis_lpush(L, v)`      | `LPUSH K V`       | 左侧推入                 |
| `redis_rpush(L, v)`      | `RPUSH K V`       | 右侧推入                 |
| `redis_lpop(L)`          | `LPOP K`          | 左侧弹出                 |
| `redis_rpop(L)`          | `RPOP K`          | 右侧弹出                 |
| `redis_lrange(L, s, e)`  | `LRANGE K S E`    | 范围获取                 |
| `redis_llen(L)`          | `LLEN K`          | 长度                     |
| `redis_lindex(L, i)`     | `LINDEX K I`      | 索引获取                 |
| `redis_sadd(S, v)`       | `SADD K V`        | 添加集合元素             |
| `redis_srem(S, v)`       | `SREM K V`        | 删除集合元素             |
| `redis_sismember(S, v)`  | `SISMEMBER K V`   | 成员检查                 |
| `redis_smembers(S)`      | `SMEMBERS K`      | 获取所有成员             |
| `redis_scard(S)`         | `SCARD K`         | 基数                     |
| `redis_sunion(A, B)`     | `SUNION K1 K2`    | 并集                     |
| `redis_sinter(A, B)`     | `SINTER K1 K2`    | 交集                     |
| `redis_zadd(Z, sc, m)`   | `ZADD K SC M`     | 添加有序集合元素         |
| `redis_zrem(Z, m)`       | `ZREM K M`        | 删除有序集合元素         |
| `redis_zscore(Z, m)`     | `ZSCORE K M`      | 获取分值                 |
| `redis_zrange(Z, s, e)`  | `ZRANGE K S E`    | 索引范围(升序)           |
| `redis_zrangebyscore(Z)` | `ZRANGEBYSCORE`   | 分值范围                 |
| `redis_zrank(Z, m)`      | `ZRANK K M`       | 排名                     |
| `redis_zcard(Z)`         | `ZCARD K`         | 基数                     |
| `redis_hset(H, f, v)`    | `HSET K F V`      | 设置哈希字段             |
| `redis_hget(H, f)`       | `HGET K F`        | 获取哈希字段             |
| `redis_hdel(H, f)`       | `HDEL K F`        | 删除哈希字段             |
| `redis_hexists(H, f)`    | `HEXISTS K F`     | 字段存在检查             |
| `redis_hkeys(H)`         | `HKEYS K`         | 获取所有字段             |
| `redis_hvals(H)`         | `HVALS K`         | 获取所有值               |
| `redis_hlen(H)`          | `HLEN K`          | 字段数                   |
| `redis_hgetall(H)`       | `HGETALL K`       | 获取所有字段和值         |

---

## 复杂度分析

### 时间复杂度

| 数据结构   | 插入       | 删除       | 查找       | 范围查询    |
|-----------|-----------|-----------|-----------|------------|
| List      | O(1)      | O(1)      | O(n)      | O(k)       |
| Set       | O(1) 均摊  | O(1) 均摊  | O(1) 均摊  | O(n)       |
| ZSet      | O(log n)  | O(log n)  | O(log n)  | O(log n+k) |
| Hash      | O(1) 均摊  | O(1) 均摊  | O(1) 均摊  | O(n)       |

其中 k = 结果数量, n = 元素总数

### 空间复杂度

| 数据结构   | 每个元素额外开销                          | 说明                          |
|-----------|-----------------------------------------|-------------------------------|
| List      | 2 个指针 (prev, next) ≈ 16 bytes        | 双向链表指针                   |
| Set       | 1 个指针 (next) + 256 bytes value       | 链地址法节点                   |
| ZSet      | 12 个指针 (forward) + 1 backward        | 跳表平均 ~1.33 层指针         |
| Hash      | 1 个指针 (next) + 64+256 bytes field/value| 链地址法节点                 |

---

## 内存布局

```
RedisZSet 内存布局示例:

header (sentinel)
  ┌───────────────────────────────────────┐
  │ score: -∞                             │
  │ forward[2] → node_B                   │
  │ forward[1] → node_A                   │
  │ forward[0] → node_A                   │
  └───────────────────────────────────────┘
                │
    ┌───────────┘
    ▼
node_A (level=2)
  ┌───────────────────────────────────────┐
  │ member: "Alice"  score: 10.5          │
  │ forward[2] → node_B                   │
  │ forward[1] → node_B                   │
  │ forward[0] → node_B                   │
  │ backward → header                     │
  └───────────────────────────────────────┘
                │
    ┌───────────┘
    ▼
node_B (level=1)
  ┌───────────────────────────────────────┐
  │ member: "Bob"    score: 20.0          │
  │ forward[1] → NULL                     │
  │ forward[0] → NULL                     │
  │ backward → node_A                     │
  └───────────────────────────────────────┘

tail → node_B
```

---

## 使用示例

```c
#include "redis_model.h"

// List example
RedisList *list = redis_list_create("mylist");
redis_rpush(list, "hello");
redis_rpush(list, "world");
redis_lpush(list, "first");
char items[10][256];
int n = redis_lrange(list, 0, -1, items, 10);
// items = ["first", "hello", "world"]

// Set example
RedisSet *set = redis_set_create("myset");
redis_sadd(set, "apple");
redis_sadd(set, "banana");
redis_sadd(set, "apple");  // 重复, 忽略
printf("Count: %d\n", redis_scard(set));  // 2

// Sorted Set example
RedisZSet *zset = redis_zset_create("leaderboard");
redis_zadd(zset, 100.0, "player1");
redis_zadd(zset, 200.0, "player2");
redis_zadd(zset, 150.0, "player3");
char members[10][256];
n = redis_zrange(zset, 0, 2, members, 10);
// members = ["player1", "player3", "player2"]

// Hash example
RedisHash *hash = redis_hash_create("user:1");
redis_hset(hash, "name", "Alice");
redis_hset(hash, "age", "28");
char val[256];
redis_hget(hash, "name", val, sizeof(val));
// val = "Alice"
```

---

## 扩展方向

1. **ziplist/intset 编码**
   - 短值/少量元素时使用紧凑编码
   - 阈值切换逻辑

2. **键空间**
   - 统一的 key→object 字典
   - TTL/过期管理
   - LRU/LFU 淘汰策略

3. **持久化**
   - RDB 快照序列化
   - AOF 命令日志

4. **发布订阅**
   - channel→subscribers 映射
   - 模式匹配 topics

5. **事务**
   - MULTI/EXEC 命令队列
   - WATCH 乐观锁

6. **并发**
   - 原子操作
   - 无锁跳表
