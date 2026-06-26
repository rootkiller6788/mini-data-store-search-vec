# mini-inverted-index — Inverted Index Internals

> 参考 Lucene Internals, Introduction to Information Retrieval (Manning)

---

## 1. 倒排索引核心概念

倒排索引 (Inverted Index) 是全文搜索引擎的基石数据结构。它将文档中每个词语映射到出现该词语的文档 ID 列表及位置信息，从而实现亚线性的查询效率——不需要扫描所有文档，只需查找与查询词相关的倒排记录。

```
正排索引 (Forward Index):
  doc_0 → { "database", "system", "stores", "data" }
  doc_1 → { "database", "management", "systems" }

倒排索引 (Inverted Index):
  "database" → [doc_0 (pos:0), doc_1 (pos:0)]
  "system"   → [doc_0 (pos:1)]
  "data"     → [doc_0 (pos:3)]
  "management" → [doc_1 (pos:1)]
```

### 1.1 数据结构组成

```c
typedef struct {
    int32_t doc_id;
    int32_t term_freq;              // 词频 TF
    int32_t positions[MAX_POSITIONS]; // 词位信息 (32个)
    int32_t num_positions;
} PostingEntry;

typedef struct {
    PostingEntry *postings;  // 动态数组
    int32_t num_docs;        // 包含该词的文档数
    int32_t doc_freq;        // 文档频率 DF
    int32_t capacity;        // 当前容量
} PostingList;

typedef struct {
    HashEntry entries[HASH_MAP_SIZE];
    int32_t num_terms;
    int32_t total_docs;
} InvertedIndex;
```

### 1.2 词典 (Dictionary / Term Dictionary)

词典存储所有索引中出现的唯一词条，并提供从词条到倒排列表的快速映射。

词典实现方案对比：

| 方案 | 查找复杂度 | 内存开销 | 适用场景 |
|------|-----------|----------|----------|
| 哈希表 | O(1) 平均 | 高 (需存储桶) | 内存索引，小规模 |
| B-Tree | O(log n) | 中 | 磁盘索引，范围查询 |
| FST (有限状态转换器) | O(len) | 低 | 大规模生产系统 |
| 跳跃表 (Skip List) | O(log n) | 中 | 并发访问友好 |

本实现采用开放寻址哈希表，使用 djb2 哈希函数：

```c
static uint32_t hash_string(const char *str) {
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        h = ((h << 5) + h) + c;  // h * 33 + c
    return h;
}
```

线性探测解决冲突，负载因子约 50% 时性能良好。

### 1.3 倒排列表 (Posting List)

倒排列表存储每个词对应于哪些文档以及频率和位置。

```
倒排列表的内部结构：
┌─────────────────────────────────────────┐
│ PostingList: "database"                 │
│   doc_freq = 4                          │
│   num_docs = 4                          │
│   ┌─────────────────────────────────┐   │
│   │ PostingEntry #0                 │   │
│   │   doc_id = 0, tf = 1, pos [0]  │   │
│   │ PostingEntry #1                 │   │
│   │   doc_id = 1, tf = 1, pos [0]  │   │
│   │ PostingEntry #2                 │   │
│   │   doc_id = 4, tf = 3, pos [...]│   │
│   │ PostingEntry #3                 │   │
│   │   doc_id = 6, tf = 1, pos [1]  │   │
│   └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

---

## 2. 倒排列表合并算法

布尔查询的核心是对多个倒排列表进行集合操作。

### 2.1 交集 (AND) — 双指针合并

```c
PostingList *posting_list_intersect(a, b) {
    i = 0, j = 0;
    while (i < a->num_docs && j < b->num_docs) {
        if (a[i].doc_id < b[j].doc_id)      i++;
        else if (a[i].doc_id > b[j].doc_id) j++;
        else { add_to_result(a[i]); i++; j++; }
    }
}
```

时间复杂度: O(|a| + |b|)，两个列表均按 doc_id 排序。

### 2.2 并集 (OR) — 双指针合并

```c
PostingList *posting_list_union(a, b) {
    while (i < |a| || j < |b|) {
        if (a[i].id < b[j].id)      { add(a[i]); i++; }
        else if (a[i].id > b[j].id) { add(b[j]); j++; }
        else                        { add(merged); i++; j++; }
    }
}
```

时间复杂度: O(|a| + |b|)。

### 2.3 排除 (NOT) — 过滤

```c
PostingList *posting_list_exclude(a, b) {
    while (i < |a|) {
        skip matching entries in b;
        if a[i] not in b: add(a[i]);
        i++;
    }
}
```

### 2.4 短语检索 (Phrase Search)

短语检索需要在倒排列表中查询位置信息：

```c
PostingList *query_phrase_search(term_a, term_b, proximity) {
    for each doc where both terms appear:
        check if any position(term_a) + proximity >= position(term_b)
        >= position(term_a)
}
```

双指针遍历位置序列，寻找满足邻近度约束的位置对。

---

## 3. 跳跃表 (Skip Lists)

跳跃表是倒排列表的一种优化结构，通过在排序列表上建立多层索引指针实现快速跳跃。

```
Level 2:  [doc_0] ────────────────────> [doc_15] ──────────> [doc_30]
Level 1:  [doc_0] ──────> [doc_5] ──────> [doc_15] ──────> [doc_25] ──> [doc_30]
Level 0:  [doc_0] → [doc_3] → [doc_5] → [doc_8] → [doc_15] → [doc_20] → [doc_25] → [doc_30]
```

在交集操作中，当我们在 Level 0 的 [doc_0] 且需要找到 >= doc_15 的文档时，可以直接跳过多层：
- Level 2 跳到 [doc_15]，跳过 4 个元素
- 如果目标在 [doc_5] 和 [doc_15] 之间，则下降到 Level 1

跳跃表的核心参数是跳跃间隔 (skipInterval) 和跳跃层数。Lucene 默认使用 skipInterval = 128，最多 5 层。

```c
// 跳跃表指针结构
typedef struct {
    int32_t doc_id;
    int64_t file_pointer;   // 底层文件偏移
    int32_t num_children;   // 跳过的文档数
} SkipPointer;
```

---

## 4. 压缩技术

### 4.1 VByte (Variable Byte Encoding)

VByte 是一种整数压缩算法，使用 7 位存储数据 + 1 位作为延续标志：

```
值 824:
  二进制: 1100111000
  VByte: [1 1001110] [0 0001000]
          ^           ^
          继续         结束
```

编码规则：
- 每字节高 1 位是标志位 (1=继续, 0=结束)
- 低 7 位存储有效数据
- 小数值用更少字节: 1-127 用 1 字节，128-16383 用 2 字节

```c
void vbyte_encode(uint32_t value, uint8_t *buf, int *len) {
    *len = 0;
    while (value >= 128) {
        buf[(*len)++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    buf[(*len)++] = (uint8_t)(value & 0x7F);
}

uint32_t vbyte_decode(const uint8_t *buf, int *pos) {
    uint32_t value = 0, shift = 0;
    while (buf[*pos] & 0x80)
        value |= (uint32_t)(buf[(*pos)++] & 0x7F) << shift, shift += 7;
    value |= (uint32_t)(buf[(*pos)++] & 0x7F) << shift;
    return value;
}
```

### 4.2 Delta 编码

文档 ID 不直接存储绝对值，而是存储相邻文档 ID 的差值 (d-gap)：

```
原始 doc_ids: [5, 23, 24, 87]
delta 编码:   [5, 18, 1, 63]
VByte 后显著减小平均值。
```

### 4.3 Simple9 和 PForDelta

| 算法 | 特点 | 压缩率 | 解压速度 |
|------|------|--------|----------|
| VByte | 简单通用 | 中 | 快 |
| Simple9 | 同组相同位数 | 中 | 快 |
| PForDelta | 异常值单独编码 | 高 | 较快 |
| Elias-Fano | 准随机序列最优 | 高 | 中等 |
| SIMD-BP128 | 128-bit SIMD | 高 | 最快 |

### 4.4 Frame of Reference (FOR)

对一组文档 ID 中的 delta 值，计算最小所需位数，所有值使用相同位数存储，并附上游标基准值。

---

## 5. 位置索引 (Positional Index)

位置索引是支持短语查询和邻近查询的基础。每个倒排记录中除了文档 ID 外，还存储词在文档中出现的所有位置。

### 5.1 为什么需要位置索引？

| 查询类型 | 是否需要位置索引 | 示例 |
|----------|-----------------|------|
| 单词语查询 | 否 | "database" |
| AND/OR 查询 | 否 | "apple AND orange" |
| 短语查询 | 是 | "new york" |
| 邻近查询 | 是 | "apple ~3 orange" |
| 窗口查询 | 是 | window(5, apple, orange) |

### 5.2 位置信息存储方案

```
不存位置 (非位置索引):
  "database" → [doc_0, doc_3, doc_5]

存储位置 (位置索引):
  "database" → [doc_0: [0, 12, 27], doc_3: [5, 18], doc_5: [2]]
```

位置信息占用的空间大约是索引大小的 2-4 倍，但对短语检索至关重要。

---

## 6. 索引构建策略

### 6.1 原位构建 (In-place Building)

```
while (more documents) {
    read document d
    tokenize(d) → tokens[]
    for each token t in tokens:
        add (d.id, position) to posting_list[t]
}
```

简单直接，适用于小规模索引。内存消耗大，因为所有倒排列表同时在内存中修改。

### 6.2 基于排序的构建 (Sort-based / BSBI)

```
Phase 1: 生成 term-docID 对
Phase 2: 按 term 排序
Phase 3: 合并相同 term，生成倒排列表
```

Lucene 使用类似的思路，将索引数据先写入段 (segment)，再合并。

### 6.3 SPIMI (Single-Pass In-Memory Indexing)

每当内存满时，将当前部分的倒排索引冲刷到磁盘，最后将所有段合并。

---

## 7. 动态索引与更新

| 策略 | 说明 | 优缺点 |
|------|------|--------|
| 重建 | 修改后全量重建 | 简单，不实用 |
| 临时索引+合并 | 新文档写入内存索引，定期merge | Lucene 默认策略 |
| 对数合并 | 大小相似的段两两合并 | 摊销 O(log n) |
| 分层合并 | 按大小分层，各层独立合并 | 写入吞吐高 |

---

## 8. 索引统计信息

| 统计量 | 符号 | 含义 |
|--------|------|------|
| Term Frequency (TF) | tf(t,d) | 词 t 在文档 d 中的出现次数 |
| Document Frequency (DF) | df(t) | 包含词 t 的文档数量 |
| Collection Frequency | cf(t) | 词 t 在整个文档集合中的总出现次数 |
| Document Length | |d| | 文档 d 的词语数量 |
| Average Document Length | avgdl | 文档平均长度 |

这些统计量是 TF-IDF 和 BM25 等评分公式的基础输入。

---

## 9. 倒排索引中的权衡

| 维度 | 选项 | 权衡 |
|------|------|------|
| 词典结构 | 哈希表 vs B-Tree vs FST | 速度 vs 范围查询 vs 紧凑 |
| 是否存位置 | 位置 vs 非位置索引 | 短语查询 vs 索引大小 |
| 是否压缩 | 原始 vs VByte vs PForDelta | 速度 vs 存储空间 |
| 是否存词频 | 完整 vs 二进制 | 评分精度 vs 索引大小 |
| 是否存文档长度 | 是 vs 否 | 评分归一化 vs 额外存储 |

---

## 10. 相关阅读

- Manning, Raghavan, Schutze. *Introduction to Information Retrieval*, Ch.1-4
- Lucene Index File Formats: https://lucene.apache.org/core/documentation.html
- Zobel & Moffat. *Inverted files for text search engines*, ACM Computing Surveys, 2006
- Buttcher, Clarke, Cormack. *Information Retrieval: Implementing and Evaluating Search Engines*
