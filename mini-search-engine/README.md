# mini-search-engine — 全文搜索引擎 (C 语言实现)

> 参考 Lucene Internals, Elasticsearch, Introduction to Information Retrieval (Manning)

## 简介

mini-search-engine 是一个用 C99 实现的教育用全文搜索引擎，仅依赖 libc 和 libm。它实现了信息检索系统的核心组件：

- **倒排索引** (Inverted Index) — 词条→文档映射，支持 AND/OR/NOT 集合操作
- **文本分析链** (Analysis Chain) — 分词、小写化、停用词过滤、Porter 词干提取
- **查询解析器** (Query Parser) — 递归下降解析，生成抽象语法树
- **相关性评分** (Scoring) — TF-IDF, BM25, 向量空间模型, 文档长度归一化
- **搜索引擎** (Search Engine) — 协调所有模块，提供统一索引与搜索 API

## 目录结构

```
mini-search-engine/
├── include/
│   ├── inverted_index.h    # 倒排索引接口
│   ├── tokenizer.h         # 分词器与分析器接口
│   ├── scoring.h           # 评分函数接口
│   ├── query_parser.h      # 查询解析器接口
│   └── search_engine.h     # 搜索引擎协调器接口
├── src/
│   ├── inverted_index.c    # 倒排索引实现 (250+ 行)
│   ├── tokenizer.c         # 分词器实现 (200+ 行)
│   ├── scoring.c           # 评分函数实现 (100+ 行)
│   ├── query_parser.c      # 查询解析器实现 (250+ 行)
│   └── search_engine.c     # 搜索引擎实现 (100+ 行)
├── examples/
│   ├── index_demo.c        # 构建索引 + 词条统计
│   ├── scoring_demo.c      # TF-IDF vs BM25 对比
│   └── boolean_query_demo.c# 布尔 + 短语查询
├── demos/
│   ├── mini-inverted-index/ # 倒排索引原理详解
│   └── mini-text-search/   # 全文搜索原理详解
├── docs/
│   ├── course-alignment.md          # 课程对应关系
│   └── search-engine-architecture.md# 架构文档
├── Makefile
└── README.md
```

## 快速开始

### 构建

```bash
make
```

### 运行示例

```bash
# 索引演示：构建倒排索引并查询词条
make run-index-demo

# 评分对比：TF-IDF vs BM25 对相同查询的排序差异
make run-scoring-demo

# 布尔查询：AND/OR/NOT 组合查询求值
make run-boolean-demo
```

### 清理

```bash
make clean
```

## 核心 API

### 搜索引擎 (高层 API)

```c
SearchEngine engine;
SearchResult results[10];

engine_init(&engine);
engine_index_document(&engine, 0, "Doc Title", "Document content text...");
engine_index_document(&engine, 1, "Another", "Another document body...");

int n = engine_search(&engine, "document text", results, 10);
engine_print_results(results, n);

engine_free(&engine);
```

### 倒排索引 (底层 API)

```c
InvertedIndex idx;
index_init(&idx);
index_add_doc(&idx, doc_id, token_array, &token_count);

const PostingList *pl = index_search_term(&idx, "database");
// pl->doc_freq = 4, pl->postings[0].doc_id = ...

PostingList *result = posting_list_intersect(pl_a, pl_b);

index_free(&idx);
```

### 分析器

```c
Analyzer analyzer;
analyzer_init(&analyzer, TOKENIZER_STANDARD,
              FILTER_LOWERCASE | FILTER_STOP_WORDS | FILTER_STEMMER);

Token tokens[MAX_TOKENS];
int n = analyzer_analyze(&analyzer, "The database systems are running!", tokens, MAX_TOKENS);
// tokens = ["databas", "system", "run"]
```

### 评分

```c
Scorer scorer;
scorer_init(&scorer, total_docs, avgdl);

double tfidf = score_tfidf(term_freq, doc_freq, total_docs);
double bm25  = score_bm25(term_freq, doc_freq, total_docs, doc_len, avgdl, 1.2, 0.75);
double combined = score_combined((double[]){tfidf, bm25}, 2);
```

### 查询解析

```c
QueryNode *tree = query_parse("apple AND orange NOT banana");
PostingList *results = query_evaluate(tree, &index);
query_free_node(tree);
```

## 设计约束

- **C99 标准**: 变量声明在块首、`//` 注释可用
- **仅 libc + libm**: 无第三方依赖
- **固定上界**: 通过宏定义限制最大词条、文档、倒排列表大小
- **显式内存管理**: 所有堆分配均有对应的 free

## 参考

- Manning, Raghavan, Schutze. *Introduction to Information Retrieval*. Cambridge, 2008
- Robertson & Zaragoza. *The Probabilistic Relevance Framework: BM25 and Beyond*, 2009
- Porter, M.F. *An Algorithm for Suffix Stripping*, Program 14(3), 1980
- Lucene Internals: https://lucene.apache.org/core/
- Elasticsearch Guide: https://www.elastic.co/guide/en/elasticsearch/reference/
