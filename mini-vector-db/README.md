# mini-vector-db — 向量数据库 (C 语言实现)

> 参考 FAISS (Meta), Milvus, Annoy (Spotify), HNSW paper

轻量级向量数据库教学实现，包含四种经典 ANN 索引算法：HNSW、IVF-PQ、LSH 和精确 KNN。

## Module Status: COMPLETE ✅

- **L1-L6**: Complete
- **L7**: Complete (3+ applications)
- **L8**: Partial (k-means++ silhouette, polysemous codes, standardization)
- **L9**: Partial (documented, JL lemma verified)

**include/ + src/ 总行数: 4102** (≥ 3000 ✅)

## 目录结构

```
mini-vector-db/
├── include/
│   ├── vector_math.h          # 向量数学工具
│   ├── distance_metrics.h     # 距离度量框架 (L1/L2/L4)
│   ├── exact_knn.h            # 精确 KNN 搜索
│   ├── hnsw.h                 # HNSW 索引
│   ├── ivf_pq.h               # IVF + 乘积量化
│   ├── lsh.h                  # 局部敏感哈希
│   ├── vector_db.h            # 向量数据库高层 API (L6/L7)
│   ├── serialization.h        # 二进制序列化 (L4/L6)
│   ├── index_config.h         # 索引配置/构建器 (L3/L5)
│   ├── pq_full.h              # 完整 PQ: ADC/SDC (L5/L8)
│   ├── kmeans_pp.h            # k-means++ 初始化 (L5/L8)
│   ├── dimensionality.h       # PCA/JL 降维 (L4/L5)
│   └── index_eval.h           # 评估/基准测试 (L6/L7)
├── src/
│   ├── vector_math.c          # 向量运算
│   ├── distance_metrics.c     # 8种距离度量 + 度量公理验证
│   ├── exact_knn.c            # 精确 KNN + top-k 堆
│   ├── hnsw.c                 # HNSW 分层图搜索
│   ├── ivf_pq.c               # IVF + 乘积量化
│   ├── lsh.c                  # 局部敏感哈希
│   ├── vector_db.c            # 向量数据库 (集合/CRUD/持久化)
│   ├── serialization.c        # 端序感知二进制 I/O + CRC32
│   ├── index_config.c         # 索引构建器 + 自动调参
│   ├── pq_full.c              # ADC/SDC/多义码/编码解码
│   ├── kmeans_pp.c            # k-means++ + 肘部法则 + 轮廓系数
│   ├── dimensionality.c       # PCA (幂迭代) + JL 引理验证
│   └── index_eval.c           # Recall@K/mAP/nDCG/QPS 基准
├── tests/
│   └── test_all.c             # 13 项综合测试
├── examples/
│   ├── knn_brute_demo.c       # 精确 KNN 示例
│   ├── hnsw_demo.c            # HNSW 构建与搜索
│   └── ann_compare_demo.c     # 四种算法综合对比
├── demos/
│   ├── mini-ann-index/README.md
│   └── mini-vector-search/README.md
├── docs/
│   ├── course-alignment.md
│   └── vector-database-architecture.md
├── Makefile
└── README.md
```

## 九层知识覆盖摘要

| Level | 名称 | 状态 | 关键实现 |
|-------|------|------|----------|
| **L1** | Definitions | Complete | Vector, KNNResult, HNSWGraph, IVFIndex, LSHTable, PQCode, VDBConfig, IndexConfig, DistanceMetric |
| **L2** | Core Concepts | Complete | 向量空间运算, ANN搜索, 距离度量抽象, 向量数据库CRUD, 索引生命周期 |
| **L3** | Engineering | Complete | MaxHeap top-k, HNSW分层图, IVF倒排文件, PQ子空间编码, 索引构建器模式, 端序感知序列化 |
| **L4** | Standards/Theorems | Complete | 度量空间公理, 三角不等式, JL引理, 维度灾难, CRC32, IEEE 754, 率失真理论 |
| **L5** | Algorithms/Methods | Complete | 暴力KNN, HNSW贪心搜索, k-means聚类, PQ编码/解码, ADC/SDC, PCA幂迭代, 随机投影 |
| **L6** | Canonical Problems | Complete | 向量数据库完整实现(SAVE/LOAD/CRUD), 四种ANN索引对比, 序列化持久化 |
| **L7** | Applications | Complete | Recall@K评估, mAP/nDCG指标, QPS基准测试, 参数扫描, 构建基准 |
| **L8** | Advanced | Partial | k-means++ 轮廓系数, 多义码预过滤, z-score标准化, 成对t检验, PQ熵分析 |
| **L9** | Industry | Partial | JL引理验证, 维度灾难实证, FAISS架构文档(见docs/) |

## 核心定义列表

- `Vector` — 高维浮点向量 (L1)
- `Neighbor` / `KNNResult` — KNN搜索结果 (L1)
- `HNSWNode` / `HNSWGraph` — 分层可导航小世界图 (L1)
- `KMeans` / `PQCode` / `PQCodebook` / `IVFIndex` — IVF-PQ索引 (L1)
- `LSHHash` / `LSHBucket` / `LSHTable` — 局部敏感哈希表 (L1)
- `DistanceMetric` / `MetricType` — 距离度量框架 (L1)
- `VDBConfig` / `VDBCollection` / `VectorDB` — 向量数据库 (L1)
- `IndexConfig` / `IndexStrategy` — 索引配置构建器 (L1)
- `PQDistanceTable` — ADC距离查找表 (L2)

## 核心定理列表

| 定理 | 层 | 实现位置 |
|------|-----|----------|
| 度量空间公理 (非负/对称/三角不等式) | L4 | distance_metrics.c |
| Johnson-Lindenstrauss 引理 (ε-距离保持) | L4 | dimensionality.c |
| Eckart-Young 定理 (PCA最优重构) | L4 | dimensionality.c |
| 维度灾难 (近邻距离比→1) | L4 | dimensionality.c |
| Lloyd算法单调收敛 | L4 | kmeans_pp.c |
| 率失真理论 (PQ量化误差界) | L4 | pq_full.h (文档) |
| k-means++ O(log k)竞争比 | L5 | kmeans_pp.c |

## 核心算法列表

| 算法 | 复杂度 | 实现位置 |
|------|--------|----------|
| Brute-Force KNN (max-heap) | O(N·d·log k) | exact_knn.c |
| HNSW 分层图搜索 | O(log N) avg | hnsw.c |
| IVF-PQ (ADC distance) | O(nprobe·N/nlist) | ivf_pq.c + pq_full.c |
| LSH (随机投影哈希) | O(L·K·d) | lsh.c |
| k-means++ 聚类 | O(k·n·d·iter) | kmeans_pp.c |
| PCA 幂迭代 | O(k·d²·iter) | dimensionality.c |
| 随机投影 (JL变换) | O(k·d) | dimensionality.c |

## 经典问题列表

1. **ANN搜索**: 四种索引算法在统一数据集上的Recall@K对比 (examples/ann_compare_demo.c)
2. **向量数据库持久化**: 二进制格式 SAVE/LOAD (vector_db.c + serialization.c)
3. **索引构建基准**: HNSW/IVF/LSH构建时间对比 (index_eval.c)
4. **参数调优扫描**: ef_search/nprobe 的 Recall-QPS tradeoff (index_eval.c)

## 九校课程映射

| 学校 | 相关课程 | 本模块对应 |
|------|---------|-----------|
| **MIT** | 6.046 Design & Analysis of Algorithms | k-means++, PCA, JL Lemma |
| **Stanford** | CS 246 Mining Massive Datasets | LSH, ANN search |
| **Berkeley** | CS 267 HPC | PQ向量化, SIMD distance |
| **CMU** | 15-445 Database Systems | IVF index, WAL, serialization |
| **UT Austin** | CS 395T Systems for ML | Vector DB architecture |
| **ETH** | 263-5210 Advanced ML | k-means++, silhouette |
| **Cambridge** | Part II: Information Retrieval | Recall@K, mAP, nDCG |
| **清华** | 数据挖掘 | 聚类分析, 降维 |
| **Georgia Tech** | CS 7641 ML | PAC learning bounds |

## 快速开始

### 编译与测试

```bash
make all       # 编译所有示例
make test      # 运行 13 项综合测试 (一键通过 ✅)
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

## 距离度量支持

| 度量 | 公式 | 类型 | 适用场景 |
|------|------|------|----------|
| L² (Euclidean) | sqrt(Σ(aᵢ-bᵢ)²) | metric | 通用 |
| L¹ (Manhattan) | Σ\|aᵢ-bᵢ\| | metric | 稀疏向量 |
| L∞ (Chebyshev) | max\|aᵢ-bᵢ\| | metric | 仓库物流 |
| Cosine | 1 - cos(θ) | pseudo | 文本相似度 |
| Inner Product | Σ aᵢbᵢ | pseudo | 推荐系统 |
| Jaccard | 1 - \|A∩B\|/\|A∪B\| | metric | 集合相似度 |
| Hamming | Σ[aᵢ≠bᵢ] | metric | 二进制向量 |
| Squared L² | Σ(aᵢ-bᵢ)² | pseudo | ANN索引加速 |

## API 概览

### 向量数据库 (vector_db.h)

```c
VectorDB db;
vdb_init(&db);

VDBConfig cfg = {
    .name = "docs", .dimension = 128,
    .index_type = VDB_INDEX_HNSW,
    .max_vectors = 100000
};
vdb_create_collection(&db, &cfg);
VDBCollection *col = vdb_get_collection(&db, "docs");

vdb_insert(col, &vec, id);
KNNResult result;
vdb_search(col, &query, 10, &result);

vdb_save(&db, "db.vdb");
vdb_load(&db, "db.vdb");
```

### 距离度量 (distance_metrics.h)

```c
float d = metric_distance(&a, &b, METRIC_L2);
int ok = metric_check_triangle(&a, &b, &c, METRIC_L2);
int idx = metric_nearest(dataset, n, &query, METRIC_COSINE, &best_dist);
```

### 降维 (dimensionality.h)

```c
float eigenvecs[K][DIM_MAX], eigenvals[K];
dim_pca(vectors, n, k, cov_matrix, eigenvecs, eigenvals);

// Johnson-Lindenstrauss 随机投影
float R[K][DIM_MAX];
dim_random_projection_matrix(R, k, d);
float pass = dim_verify_jl(vectors, n, R, k, 0.5);
```

### 聚类 (kmeans_pp.h)

```c
kmeans_pp_init(vectors, n, dim, k, centroids);
int iters = kmeans_cluster(vectors, n, dim, k, 100, centroids, assignments);
float sil = kmeans_silhouette(vectors, n, dim, assignments, k);
```

### PQ 完整实现 (pq_full.h)

```c
PQDistanceTable table;
pq_compute_distance_table(&codebook, residual, M, ks, subdim, &table);
float dist = pq_adc_distance(&table, &code, M);

// 多义码预过滤
if (pq_hamming_prefilter(&code_q, &code_db, M, threshold)) {
    // compute full distance
}
```

### 评估 (index_eval.h)

```c
float recall = eval_recall_at_k(&gt, &approx, k);
float ndcg   = eval_ndcg_at_k(&gt, &approx, k);
double qps   = eval_benchmark_qps(search_fn, dataset, n, queries, nq, k, 10);
eval_recall_qps_sweep(&hnsw, &ivf, &lsh, ...);
```

## 参数调优

| 参数 | 算法 | 推荐范围 | 说明 |
|------|------|----------|------|
| M | HNSW | 12-48 | 连接数 |
| efConstruction | HNSW | 100-500 | 构建搜索宽度 |
| efSearch | HNSW | 16-512 | 查询搜索宽度 |
| nlist | IVF | sqrt(N)~4×sqrt(N) | 聚类中心数 |
| nprobe | IVF | 1-32 | 查询探测聚类数 |
| M (PQ) | IVF-PQ | 8-64 | 子量化器数 |
| nbits | PQ | 8 | 子码字位数 |
| L | LSH | 10-100 | 哈希表数 |
| K | LSH | 4-16 | 每表哈希函数数 |

## 依赖

- C99 编译器 (GCC / Clang)
- libc + libm

## 许可

Educational Use Only
