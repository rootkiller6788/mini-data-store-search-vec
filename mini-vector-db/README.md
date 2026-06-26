# mini-vector-db — 向量数据库 (C 语言实现)

> 参考 FAISS (Meta), Milvus, Annoy (Spotify), HNSW paper

轻量级向量数据库教学实现，包含四种经典 ANN 索引算法：HNSW、IVF-PQ、LSH 和精确 KNN。

## 目录结构

```
mini-vector-db/
├── include/
│   ├── vector_math.h      # 向量数学工具
│   ├── exact_knn.h        # 精确 KNN 搜索
│   ├── hnsw.h             # HNSW 索引
│   ├── ivf_pq.h           # IVF + 乘积量化
│   └── lsh.h              # 局部敏感哈希
├── src/
│   ├── vector_math.c      # 向量运算实现
│   ├── exact_knn.c        # 精确搜索实现
│   ├── hnsw.c             # HNSW 实现
│   ├── ivf_pq.c           # IVF-PQ 实现
│   └── lsh.c              # LSH 实现
├── examples/
│   ├── knn_brute_demo.c   # 精确 KNN 示例
│   ├── hnsw_demo.c        # HNSW 构建与搜索
│   └── ann_compare_demo.c # 四种算法综合对比
├── demos/
│   ├── mini-ann-index/README.md      # ANN 索引综述
│   └── mini-vector-search/README.md  # 向量搜索完整流程
├── docs/
│   ├── course-alignment.md             # 课程对齐文档
│   └── vector-database-architecture.md # 向量数据库架构
├── Makefile
└── README.md
```

## 快速开始

### 编译

```bash
make all
```

### 运行示例

```bash
make run-knn       # 精确 KNN 搜索
make run-hnsw      # HNSW 构建与搜索
make run-compare   # 四种 ANN 算法对比
```

### 清理

```bash
make clean
```

## 算法概述

| 算法 | 类型 | 召回率 | 速度 | 内存 |
|------|------|--------|------|------|
| Brute Force | 精确 | 100% | O(N) | O(N) |
| HNSW | 图 | 97-99% | O(log N) | 高 |
| IVF-PQ | 量化+分区 | 90-97% | O(nprobe×N/nlist) | 低 |
| LSH | 哈希 | 70-90% | O(L) | 中 |

## API 概览

### 向量运算 (vector_math.h)

```c
Vector a = {.dim = 128};
vec_fill_random(&a, 128);

float dist = vec_euclidean_dist(&a, &b);
float sim  = vec_cosine_similarity(&a, &b);
float dot  = vec_dot_product(&a, &b);
vec_l2_normalize(&a);
```

### 精确 KNN (exact_knn.h)

```c
KNNResult result = knn_search(dataset, n, &query, k);
knn_print_result(&result);
```

### HNSW (hnsw.h)

```c
HNSWGraph graph;
hnsw_init(&graph, 16, 200);
hnsw_insert(&graph, &vector, id);

KNNResult result;
hnsw_search(&graph, &query, k, 64, &result);
hnsw_print_stats(&graph);
```

### IVF-PQ (ivf_pq.h)

```c
IVFIndex index;
ivf_init(&index);
ivf_train(&index, vectors, n, 256);
ivf_add(&index, &vector, id);

KNNResult result;
ivf_search(&index, &query, k, 10, &result);
```

### LSH (lsh.h)

```c
LSHTable table;
lsh_init(&table);
lsh_insert(&table, &vector, id);

KNNResult result;
lsh_search(&table, &query, k, 1, &result);
```

## 参数调优

| 参数 | 算法 | 推荐范围 | 说明 |
|------|------|----------|------|
| M | HNSW | 12-48 | 连接数，越大召回越高内存越多 |
| efConstruction | HNSW | 100-500 | 构建时搜索宽度 |
| efSearch | HNSW | 16-512 | 查询时搜索宽度 |
| nlist | IVF | sqrt(N)~4×sqrt(N) | 聚类中心数 |
| nprobe | IVF | 1-32 | 查询探测聚类数 |
| M (PQ) | IVF-PQ | 8-64 | 子量化器数 |
| nbits | PQ | 8 | 子码字位数 |
| L | LSH | 10-100 | 哈希表数 |
| K | LSH | 4-16 | 每表哈希函数数 |

## 依赖

- C99 编译器 (GCC / Clang / MSVC)
- libc + libm

## 许可

Educational Use Only
