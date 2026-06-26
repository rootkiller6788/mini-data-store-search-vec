# Course Alignment

## 课程对应关系

本 mini-search-engine 项目覆盖以下课程的核心知识点：

### 1. 信息检索 (Information Retrieval)

| 知识点 | 本项目对应 | 文件 |
|--------|-----------|------|
| 倒排索引结构 | PostingEntry, PostingList, InvertedIndex | `include/inverted_index.h`, `src/inverted_index.c` |
| 词典结构 (哈希表) | HashEntry, 线性探测 | `src/inverted_index.c` |
| 倒排列表合并算法 | posting_list_intersect/union/exclude | `src/inverted_index.c` |
| 位置索引 | positions[MAX_POSITIONS] | `include/inverted_index.h` |
| 短语检索 | query_phrase_search | `src/query_parser.c` |
| TF-IDF 评分 | score_tfidf | `include/scoring.h`, `src/scoring.c` |
| BM25 评分 | score_bm25 (k1=1.2, b=0.75) | `include/scoring.h`, `src/scoring.c` |
| 向量空间模型 | score_vector_space (cosine) | `include/scoring.h`, `src/scoring.c` |
| 布尔模型 | NODE_AND/OR/NOT, query_boolean_search | `include/query_parser.h`, `src/query_parser.c` |
| 查询解析 | query_parse (递归下降) | `src/query_parser.c` |
| 索引压缩 (理论) | VByte, Delta, PForDelta 讨论 | `demos/mini-inverted-index/README.md` |
| 跳跃表 | Skip Pointer 讨论 | `demos/mini-inverted-index/README.md` |
| 评估指标 | MAP, NDCG, MRR 概述 | `demos/mini-text-search/README.md` |

### 2. 自然语言处理 (NLP)

| 知识点 | 本项目对应 | 文件 |
|--------|-----------|------|
| 分词 (Tokenization) | Tokenizer (WHITESPACE, STANDARD, NGRAM) | `include/tokenizer.h`, `src/tokenizer.c` |
| 词干提取 (Stemming) | porter_stem (简化 Porter Stemmer) | `src/tokenizer.c` |
| 停用词 (Stop Words) | stop_word_list, FILTER_STOP_WORDS | `src/tokenizer.c` |
| 小写化 (Normalization) | FILTER_LOWERCASE | `src/tokenizer.c` |
| 分析链 (Analysis Chain) | analyzer_analyze | `include/tokenizer.h`, `src/tokenizer.c` |
| N-Gram | TOKENIZER_NGRAM (2-3 gram) | `src/tokenizer.c` |

### 3. 数据结构

| 知识点 | 本项目对应 |
|--------|-----------|
| 哈希表 (开放寻址) | InvertedIndex.entries[HASH_MAP_SIZE] |
| 动态数组 | PostingList.postings (realloc 扩容) |
| 树结构 (AST) | QueryNode 查询语法树 |
| 双指针合并 | posting_list_intersect/union |
| 排序 (qsort) | query_result_top_k |

### 4. 软件工程

| 知识点 | 本项目对应 |
|--------|-----------|
| C99 标准 | 所有源文件使用 C99 |
| 模块化设计 | include/ 头文件 + src/ 实现文件 |
| 内存管理 | calloc/realloc/free 显式管理 |
| Makefile 构建 | GNU Make |
| 示例与文档 | examples/, demos/, docs/ |

### 5. 数据库系统

| 知识点 | 本项目对应 |
|--------|-----------|
| 索引结构 | 倒排索引 (vs B+Tree, Hash Index) |
| 查询处理 | 查询解析 → 求值 → 排序 |
| 查询优化 | 倒排列表先小后大合并 |
| 统计量维护 | DF (文档频率), TF (词频) |

### 6. 分布式系统 (扩展阅读)

| 知识点 | 笔记 |
|--------|------|
| 分片 (Sharding) | 倒排索引按文档ID或词条水平分片 |
| 复制 (Replication) | 主副本索引 + 从副本索引 |
| 一致性 (Consistency) | Elasticsearch 的 primary-replica 模型 |
| MapReduce | 索引构建可表示为 Map (tokenize) + Reduce (merge postings) |
