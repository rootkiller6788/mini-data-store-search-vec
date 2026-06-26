# mini-text-search — 全文搜索引擎原理

> 参考 Elasticsearch, Introduction to Information Retrieval (Manning)

---

## 1. 全文搜索概览

全文搜索 (Full-Text Search) 是信息检索的核心技术，允许用户使用自然语言查询在海量非结构化文本中查找相关文档。

### 1.1 搜索流水线

```
用户查询 "database search performance"
         │
         ▼
  ┌─ 分析链 (Analysis Chain) ─┐
  │ 1. 分词 (Tokenization)    │
  │ 2. 小写化 (Lowercasing)   │
  │ 3. 去停用词 (Stop Words)  │
  │ 4. 词干提取 (Stemming)    │
  └───────────────────────────┘
         │ ["databas", "search", "perform"]
         ▼
  ┌─ 查询解析 (Query Parsing) ─┐
  │ 构建查询语法树             │
  │ AND/OR/NOT/PHRASE 节点     │
  └───────────────────────────┘
         │
         ▼
  ┌─ 倒排列表检索 ────────────┐
  │ 从倒排索引获取 posting    │
  │ 合并/过滤 postings        │
  └───────────────────────────┘
         │ [doc_3, doc_7, doc_12, ...]
         ▼
  ┌─ 相关性评分 ──────────────┐
  │ TF-IDF / BM25 / 向量空间  │
  │ 综合评分 + 排序            │
  └───────────────────────────┘
         │ [doc_7:0.92, doc_3:0.78, doc_12:0.45]
         ▼
      返回 Top-K 结果
```

---

## 2. 分析链 (Analysis Chain)

分析链将原始文本转换为用于索引和查询的标准化词条序列。

### 2.1 分词器 (Tokenizer)

| 类型 | 算法 | 示例 |
|------|------|------|
| Whitespace | 按空白字符分割 | "hello world!" → ["hello", "world!"] |
| Standard | 按空白+标点分割 | "hello world!" → ["hello", "world"] |
| N-Gram (2-3) | 滑动窗口子串 | "hello" → ["he","el","ll","lo","hel","ell","llo"] |
| CJK | 大颗粒/小颗粒分切 | 中文"搜索引擎" → ["搜索","索引","引擎","搜索引擎"] |
| Edge N-Gram | 前缀子串 | "hello" → ["h","he","hel","hell","hello"] |

N-Gram 分词适用于：
- 拼写错误容忍 (fuzzy search): "database" → ngrams 可匹配 "databese"
- 子串匹配: "data" 可以匹配到 "database"
- 无空格语言的文本 (日语、泰语等)

### 2.2 小写化 (Lowercasing)

将 ASCII 字母统一为小写形式，确保 "Database"、"database"、"DATABASE" 映射到同一个词条。

```c
void to_lowercase(char *s) {
    while (*s) {
        if (*s >= 'A' && *s <= 'Z') *s += 32;
        s++;
    }
}
```

### 2.3 停用词过滤 (Stop Word Removal)

停用词是出现频率极高但对检索贡献极小的功能词。常见停用词包括 "the", "a", "is", "and", "of", "in" 等。

移除停用词的权衡：

| 优点 | 缺点 |
|------|------|
| 索引体积减小 | "to be or not to be" 完全丢失 |
| 查询速度提升 | "the who" 乐队名无法检索 |
| 减少噪音匹配 | 短语 "man in the moon" 变得不精确 |

现代搜索引擎 (如 Elasticsearch) 默认不再移除停用词，而是通过评分机制自然降低其权重。Lucene 使用 `stop` 过滤器配合 `common_grams` 标记过滤器。

### 2.4 词干提取 (Stemming)

词干提取将变形词还原为词干形式，将 "running", "runs", "ran" 都映射到 "run"。

#### Porter Stemmer 简化版步骤

```
Step 1a: 复数/第三人称
  -SSES → -SS      (dresses → dress)
  -IES  → -I       (ponies  → poni)
  -SS   → -SS      (caress  → caress, 不变)
  -S    → ε        (cats    → cat)

Step 1b: 过去式/进行时
  -EED  → -EE      (agreed  → agree, m>=1)
  -ED   → ε        (played  → play, contains vowel)
  -ING  → ε        (playing → play, contains vowel)

Step 2-5: 进一步规约 (简化)
  -ATIONAL → -ATE   (relational → relate)
  -TIONAL  → -TION  (conditional → condition)
  -MENT    → ε      (adjustment → adjust)
  -NESS    → ε      (happiness → happi)
  -ITY     → ε      (ability → abil)
  -ENCE    → ε      (dependence → depend)
  -ANCE    → ε      (allowance → allow)
```

#### 其他词干提取算法

| 算法 | 语言 | 特点 |
|------|------|------|
| Porter | 英语 | 经典，适度激进 |
| Porter2 (Snowball) | 多语言 | 改进版，支持多种语言 |
| Lovins | 英语 | 更激进 |
| Krovetz (KSTEM) | 英语 | 基于词典+规则，更保守 |
| Hunspell | 多语言 | 基于词典，精确但慢 |

---

## 3. 查询解析与求值 (Query Parsing & Evaluation)

### 3.1 查询语法树

查询字符串被解析为抽象语法树 (AST)：

```
输入: "apple AND orange NOT banana"

语法树:
        AND
       /   \
    apple   NOT
             \
            orange
             /
          banana  ？等等让我重画...

实际树应该是:
         AND
        /   \
    apple   NOT
           /   \
       orange  banana

嗯不对，"apple AND orange NOT banana" 应该是:

通常解析为: apple AND (orange NOT banana) 或 (apple AND orange) NOT banana

在本实现中 AND 优先级高于 NOT，解析结果:
      AND
     /   \
  apple  NOT
          \
         orange
        /
    banana  - 不，NOT 是一元操作

实际上: (apple AND orange) NOT banana

       NOT
       /
     AND
    /   \
 apple  orange
``` (banana 作为 NOT 的子节点... NOT 是一元的)

本实现的解析方式是:
- 相邻词默认 AND
- AND/OR 作为中缀二元运算符
- NOT 作为前缀一元运算符

```
apple AND orange NOT banana:

      AND
     /   \
  apple   NOT
           \
         banana
  (orange 被...)

不对，让我重新思考：
"apple AND orange NOT banana"
- apple: TERM 节点
- AND: 二元运算符，左=apple, 右=剩下的
- orange: TERM 节点
- NOT: 遇到了，需要处理

实际上本实现的解析器处理如下:
默认相邻是 AND:
apple AND orange NOT banana
= apple AND (orange AND (NOT banana))

      AND
     /   \
  apple   AND
         /   \
     orange  NOT
              \
            banana
```

### 3.2 查询求值

查询树求值是自底向上递归的：

```c
eval(node, index) {
    if node.type == TERM:  return index.search(node.term)
    if node.type == AND:   return intersect(eval(left), eval(right))
    if node.type == OR:    return union(eval(left), eval(right))
    if node.type == NOT:   return exclude(all_docs, eval(left))
    if node.type == PHRASE: return phrase_search(left, right)
    if node.type == PREFIX: return prefix_expand(node.term)
}
```

### 3.3 布尔模型

布尔模型是最简单的检索模型，文档和查询都表示为词项的布尔组合。

| 运算符 | 语义 | 集合操作 |
|--------|------|----------|
| AND | 两个词都出现 | 交集 |
| OR | 任意一个出现 | 并集 |
| NOT | 词不出现 | 差集 |

布尔模型的优点是精确可控，缺点是无法对结果排序、无法表达部分匹配。

---

## 4. 相关性评分 (Relevance Scoring)

### 4.1 TF-IDF

$$TF(t,d) = \ln(1 + f_{t,d})$$

$$IDF(t,D) = \log\frac{N + 1}{df_t + 0.5}$$

$$Score_{TF\text{-}IDF}(t,d) = TF(t,d) \times IDF(t,D)$$

TF-IDF 的基本思想：
- TF (词频): 词在文档中出现越多次，越能代表该文档
- IDF (逆文档频率): 词在整个集合中出现越少，区分度越高

TF-IDF 的变体 (SMART 记号体系):

| 变体 | TF | IDF | 归一化 |
|------|-----|------|--------|
| ltn | log(1+tf) | idf | 无 |
| lnc | log(1+tf) | idf | cosine |
| bnn | 0/1 | 1 | 无 |
| bnc | 0/1 | 1 | cosine |

### 4.2 BM25

BM25 是概率检索模型的代表，是 TF-IDF 的改进版，考虑了文档长度归一化和词频饱和。

$$BM25(t,d) = IDF(t) \times \frac{tf_{t,d} \cdot (k_1 + 1)}{tf_{t,d} + k_1 \cdot (1 - b + b \cdot \frac{|d|}{avgdl})}$$

$$IDF(t) = \log\frac{N - df_t + 0.5}{df_t + 0.5}$$

参数说明：

| 参数 | 默认值 | 含义 |
|------|--------|------|
| k1 | 1.2 | 词频饱和参数，越大词频影响越大 |
| b | 0.75 | 文档长度归一化强度，0=不归一化，1=完全归一化 |

BM25 vs TF-IDF 的关键区别：

```
TF-IDF: 词频无限增长，分数永远增长
BM25:   词频趋于饱和，约在 tf=6-10 时接近上限

score
  ^
1.0│         ____--- BM25 (饱和)
   │      __/
0.5│   _/
   │ _/          TF-IDF (无界)
0.0│/____________________> tf
   0   5   10  15  20
```

BM25 是目前工业界的默认选择，被 Elasticsearch 和 Lucene 作为默认评分函数。

### 4.3 向量空间模型 (Vector Space Model)

将文档和查询表示为高维空间中的向量，使用余弦相似度比较：

$$cosine(q,d) = \frac{\vec{q} \cdot \vec{d}}{|\vec{q}| \cdot |\vec{d}|} = \frac{\sum q_i d_i}{\sqrt{\sum q_i^2} \cdot \sqrt{\sum d_i^2}}$$

向量空间模型的优点是可以自然地处理部分匹配和长文档。

### 4.4 综合评分

实际搜索引擎通常组合多种评分信号：

```c
double score_combined(double *scores, int32_t n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += scores[i];
    return sum / n;  // 简单平均
}
```

Elasticsearch 使用 `function_score` 查询组合多种信号，包括：
- 文本相关性 (text relevance)
- 流行度 (popularity boost)
- 时效性 (recency boost)
- 地理位置邻近 (geo-distance decay)
- 自定义脚本分数 (script score)

### 4.5 评分性能优化

| 优化技术 | 说明 |
|----------|------|
| WAND 算法 | 只对可能进入 top-K 的文档完整评分 |
| MaxScore | 维护各词最大贡献，提前剪枝 |
| Block-Max WAND | WAND + 跳跃表 + 块最大值 |
| 两阶段排序 | 粗排 (轻量) + 精排 (完整评分) |
| Learning to Rank | 使用机器学习模型替代公式 |

---

## 5. 分面搜索 (Faceted Search)

分面搜索允许用户通过预先定义的类别 (facets) 来过滤和导航搜索结果。常见于电商、文献检索系统。

### 5.1 分面类型

| 类型 | 示例 | 数据结构 |
|------|------|----------|
| Term Facets | 品牌：Apple, Samsung, Xiaomi | term → doc_count |
| Range Facets | 价格：0-100, 100-500, 500+ | range → doc_count |
| Histogram Facets | 评分：1星(23), 2星(47), 3星(112)... | bucket → doc_count |
| Date Histogram | 发布时间按天/月/年统计 | interval → doc_count |
| Geo-distance Facets | 距离 <1km, 1-5km, 5-10km | ring → doc_count |

### 5.2 实现方式

```
查询 "laptop" → 返回 1500 个结果
同时计算分面:
  Brand: Apple(320), Dell(280), HP(210), Lenovo(190), ...
  Price: 0-500(89), 500-1000(456), 1000-2000(612), 2000+(343)
  Rating: 1★(45), 2★(67), 3★(134), 4★(456), 5★(798)
```

Elasticsearch 使用 `aggregations` 框架实现分面搜索，底层使用 `Global Ordinals` 优化字符串分面的计算。

---

## 6. 索引压缩与查询处理

文本索引的体积通常很大，一个 1TB 的文本集合生成的索引可能在 200-400GB。压缩技术至关重要。

### 6.1 词典压缩

- 前缀压缩 (Front Coding): 存储公共前缀一次
- FST (有限状态转换器): 共享前后缀的最小自动机

### 6.2 倒排列表压缩

| 编码 | 每整数平均位数 |
|------|---------------|
| 无压缩 | 32 |
| VByte | ~12-16 |
| Simple9 | ~10-14 |
| PForDelta | ~6-10 |
| Elias-Fano | ~3-8 |

---

## 7. 评估指标

| 指标 | 定义 | 说明 |
|------|------|------|
| Precision | TP / (TP + FP) | 返回结果中相关的比例 |
| Recall | TP / (TP + FN) | 所有相关文档被找到的比例 |
| F1 | 2 * P * R / (P + R) | P 和 R 的调和平均 |
| MAP (Mean Avg Precision) | ∑AP / |queries| | 排序敏感的精度 |
| NDCG (Normalized DCG) | DCG / IDCG | 考虑排序位置和相关性等级 |
| MRR (Mean Reciprocal Rank) | ∑(1/rank) / |Q| | 第一个相关文档的位置 |

---

## 8. 相关系统与工具

| 系统 | 特点 |
|------|------|
| Lucene | Java 全文搜索库，倒排索引标准实现 |
| Solr | 基于 Lucene 的企业搜索平台 |
| Elasticsearch | 分布式 RESTful 搜索引擎 |
| Tantivy | Rust 实现的全文搜索引擎库 |
| Meilisearch | Rust 实现，聚焦开发者体验 |
| Typesense | C++ 实现，容错搜索 |
| Xapian | C++ 实现，概率检索模型 |
| Whoosh | Python 纯实现 |

---

## 9. 参考文献

- Manning, Raghavan, Schutze. *Introduction to Information Retrieval*. Cambridge, 2008
- Robertson & Zaragoza. *The Probabilistic Relevance Framework: BM25 and Beyond*, 2009
- Porter, M.F. *An Algorithm for Suffix Stripping*, Program 14(3), 1980
- Zobel & Moffat. *Inverted Files for Text Search Engines*, ACM Computing Surveys, 2006
- Broder et al. *Efficient Query Evaluation using a Two-Level Retrieval Process*, CIKM 2003
- Ding & Suel. *Faster Top-k Document Retrieval Using Block-Max Indexes*, SIGIR 2011
