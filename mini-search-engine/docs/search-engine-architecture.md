# Search Engine Architecture

## 系统架构概览

```
┌──────────────────────────────────────────────────────────────┐
│                     SearchEngine                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  Analyzer   │  │  Scorer     │  │   InvertedIndex     │  │
│  │             │  │             │  │                     │  │
│  │ tokenizer   │  │ tf-idf      │  │  Hash Map           │  │
│  │ lowercase   │  │ bm25        │  │  ┌───┬───┬───┬───┐ │  │
│  │ stop-words  │  │ vector-space│  │  │ K │ V │ K │ V │ │  │
│  │ stemmer     │  │ normalize   │  │  └───┴───┴───┴───┘ │  │
│  └─────────────┘  └─────────────┘  │                     │  │
│                                     │  term → PostingList│  │
│  ┌─────────────┐                    │                     │  │
│  │QueryParser  │                    │  PostingList[]      │  │
│  │             │                    └─────────────────────┘  │
│  │ parse tree  │                                              │
│  │ evaluate    │   ┌──────────────┐                          │
│  │ phrase      │   │ doc_titles[] │                          │
│  │ boolean     │   │ doc_lengths[]│                          │
│  │ top-k       │   └──────────────┘                          │
│  └─────────────┘                                              │
└──────────────────────────────────────────────────────────────┘
```

## 数据流

### 索引流程 (Indexing Pipeline)

```
Raw Document (title, content)
         │
         ▼
   analyzer_analyze()
         │
    Tokenizer → Lowercase → Remove Stop Words → Stem
         │
         ▼
   Token[] tokens
         │
         ▼
   index_add_doc(doc_id, tokens)
         │
         ▼
   For each token:
     hash_lookup(term) → HashEntry
     posting_list_add(doc_id, position)
         │
         ▼
   Update stats: total_docs, avgdl, doc_lengths[]
```

### 搜索流程 (Search Pipeline)

```
User Query: "database search performance"
         │
         ▼
   analyzer_analyze(query)
         │ → ["databas", "search", "perform"]
         ▼
   query_boolean_search("databas search perform")
         │
         ▼
   query_parse() → QueryNode tree (AND nodes)
         │
         ▼
   query_evaluate(tree, index)
         │ → for each term: search_term → PostingList
         │ → intersect/union postings per operators
         ▼
   Combined PostingList
         │
         ▼
   query_result_top_k(list, scorer)
         │ → score_tfidf + score_bm25 → combined
         │ → qsort desc by score
         ▼
   SearchResult[] → engine_print_results()
```

## 模块详解

### 1. inverted_index (倒排索引模块)

**职责**: 词条到倒排列表的映射、倒排列表管理、合并操作

**核心接口**:
- `index_init()` — 初始化索引
- `index_add_doc()` — 向索引添加文档
- `index_search_term()` — 查询单个词条的倒排列表
- `posting_list_intersect()` — AND 操作
- `posting_list_union()` — OR 操作
- `posting_list_exclude()` — NOT 操作

**内部结构**:
```
InvertedIndex
  └─ HashEntry[HASH_MAP_SIZE(2048)]
       └─ HashEntry
            ├─ term[64]
            ├─ occupied (flag)
            └─ PostingList
                 └─ PostingEntry[capacity]
                      ├─ doc_id
                      ├─ term_freq
                      └─ positions[32]
```

**哈希策略**:
- djb2 哈希函数: `h = 5381; h = h*33 + c`
- 线性探测解决冲突
- 固定 2048 桶，适合 ~1000 个唯一词条

### 2. tokenizer (分词与分析模块)

**职责**: 文本预处理，将原始文本转换为标准化词条序列

**Token 结构**:
```c
typedef struct {
    char    text[MAX_TOKEN_TEXT];  // 词条文本
    int32_t position;              // 位置序号
    int32_t start_offset;          // 原始文本中的起始偏移
} Token;
```

**分析链**:
```
原始文本
  → Tokenizer (WHITESPACE/STANDARD/NGRAM)
  → Lowercase 过滤
  → Stop Word 过滤
  → Porter Stemming
  → 输出 Token[]
```

**分词器类型**:
| 类型 | 分割规则 | 适用场景 |
|------|---------|---------|
| WHITESPACE | 仅空白字符 | 简单英文文本 |
| STANDARD | 空白+标点符号 | 通用英文文本 |
| NGRAM(2,3) | 2-3滑动窗口 | 子串匹配、模糊搜索 |

**Porter Stemmer 简化实现**:
- Step 1a: -sses → -ss, -ies → -i, -s → ε
- Step 1b: -ed, -ing 变形处理
- Step 3: -ational → -ate, -ment → ε, -ness → ε 等

### 3. scoring (评分模块)

**职责**: 计算文档与查询的相关性分数

**评分公式**:

TF-IDF:
```
TF = ln(1 + tf)
IDF = log((N + 1) / (df + 0.5))
Score = TF * IDF
```

BM25:
```
IDF = log((N - df + 0.5) / (df + 0.5))
TF_norm = tf * (k1 + 1) / (tf + k1 * (1 - b + b * |d| / avgdl))
Score = IDF * TF_norm
```

向量空间:
```
cosine(q, d) = (q · d) / (|q| * |d|)
```

**长度归一化**:
```c
norm = 1 / sqrt(1 + (doc_length - avgdl) / avgdl)
clamped(norm, 0.1, 2.0)
final = raw_score * norm
```

### 4. query_parser (查询解析模块)

**职责**: 将查询字符串解析为抽象语法树并求值

**语法规则** (简化):
```
expression := or_expr
or_expr    := and_expr ("OR" and_expr)*
and_expr   := atom (atom)*
atom       := term | "NOT" atom | "(" expression ")"
term       := [a-zA-Z0-9_]+ ("*")?  (prefix query with *)
```

**节点类型**:
| 类型 | 说明 | 子节点 | 评估方式 |
|------|------|--------|---------|
| TERM | 词条匹配 | 0 | 查倒排列表 |
| AND | 逻辑与 | 2 | 交集 |
| OR | 逻辑或 | 2 | 并集 |
| NOT | 逻辑非 | 1 | 差集 (全集 - 子列表) |
| PHRASE | 短语查询 | 2 | 位置邻近检查 |
| PREFIX | 前缀查询 | 0 | 遍历词典前缀匹配 |

### 5. search_engine (搜索引擎协调模块)

**职责**: 协调所有子模块，提供统一的索引和搜索 API

**SearchEngine 结构体**:
```c
typedef struct {
    InvertedIndex   index;           // 倒排索引
    Analyzer        analyzer;        // 文本分析器
    Scorer          scorer;          // 评分器
    int32_t         num_docs;        // 文档数量
    int32_t         total_doc_length;// 文档总长度
    char            doc_titles[][];  // 文档标题数组
    int32_t        *doc_lengths;     // 各文档长度
} SearchEngine;
```

**搜索函数流程**:
```
engine_search(query) {
    1. analyzer_analyze(query) → tokens[]
    2. join tokens → "tok1 tok2 ..."
    3. query_boolean_search(index, joined)
    4. query_result_top_k(posting_list, scorer)
    5. return results[]
}
```

## 内存布局

```
SearchEngine (sizeof: ~8KB)
├─ InvertedIndex (sizeof: ~256KB)
│  └─ HashEntry[2048] (2048 * ~128B)
│     └─ PostingList
│        └─ PostingEntry[] (heap allocated, grows by 2x)
├─ Analyzer (sizeof: ~4KB)
│  └─ stop_words[64][64]
├─ Scorer (sizeof: 16B)
├─ doc_titles[MAX_DOCS=128][MAX_TITLE_LEN=256] (~32KB)
└─ doc_lengths[128+] (heap allocated)

Total per engine: ~300KB + posting entries (varies by corpus)
```

## 并发考虑 (未实现)

本实现是单线程的。生产系统需要考虑：

1. **读写分离**: 一个写入线程 + 多个读取线程
2. **分段锁**: 对哈希表的不同桶使用不同锁
3. **无锁并发**: 使用 RCU (Read-Copy-Update) 或 hazard pointers
4. **索引更新**: copy-on-write 段合并，旧段只读

## 扩展方向

1. **增量索引**: 支持动态添加、删除文档
2. **字段索引**: 对 title/body/author 等不同字段分别索引
3. **加权查询**: 不同字段不同权重 (title^2 body)
4. **模糊查询**: Levenshtein 编辑距离 + 自动机匹配
5. **中文分词**: 集成 jieba 或类似分词器
6. **文件存储**: 倒排列表序列化到磁盘，支持 MMap
7. **分布式**: gRPC + 一致性哈希分片
