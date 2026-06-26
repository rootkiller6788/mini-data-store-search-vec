# mini-ann-index — ANN 索引技术综述

> 参考 FAISS (Meta), Milvus (Zilliz), Annoy (Spotify), NMSLIB (HNSW)

## 目录

1. [概述](#概述)
2. [近似最近邻 (ANN) 问题定义](#近似最近邻-ann-问题定义)
3. [索引算法分类](#索引算法分类)
4. [HNSW — 分层可导航小世界图](#hnsw--分层可导航小世界图)
5. [IVF — 倒排文件索引](#ivf--倒排文件索引)
6. [PQ — 乘积量化](#pq--乘积量化)
7. [IVF-PQ — 组合索引](#ivf-pq--组合索引)
8. [LSH — 局部敏感哈希](#lsh--局部敏感哈希)
9. [性能对比与权衡](#性能对比与权衡)
10. [工业界最佳实践](#工业界最佳实践)
11. [参考论文与资源](#参考论文与资源)

---

## 概述

近似最近邻搜索 (Approximate Nearest Neighbor, ANN) 是向量数据库的核心功能。
与精确 KNN (O(N×D)) 不同，ANN 在精度与速度之间做权衡，通过牺牲少量召回率
(通常 < 1%) 换取 10-1000 倍的速度提升。

### 为什么需要 ANN

| 指标 | 精确 KNN | ANN |
|------|---------|------|
| 时间复杂度 | O(N×D) | O(log N × D) |
| 百万级数据 | ~100ms | ~1ms |
| 召回率@10 | 100% | 95-99.9% |
| 内存占用 | N×D×4 bytes | N×D×4 + 索引开销 |

---

## 近似最近邻 (ANN) 问题定义

给定：
- 数据集 X = {x₁, x₂, ..., xₙ} ⊂ ℝᵈ
- 查询向量 q ∈ ℝᵈ
- 目标邻居数 k

近似最近邻搜索返回 k 个点 p₁, ..., pₖ，满足：

```
对于每个 pᵢ，dist(q, pᵢ) ≤ c × dist(q, x*ᵢ)
```

其中 x*ᵢ 是真实第 i 近邻，c 是近似因子 (c-ANN)。

### 评价指标

- **Recall@k**: |ANN_result ∩ True_k| / k
- **QPS (Queries Per Second)**: 每秒查询数
- **Build time**: 索引构建时间
- **Memory overhead**: 索引额外内存
- **Recall vs QPS 曲线**: 核心 tradeoff 可视化

---

## 索引算法分类

### 1. 基于哈希 (Hash-based)
- **LSH** (Local Sensitive Hashing)
- 随机投影将相似向量映射到相同桶
- 优点: 理论保证、构建快
- 缺点: 召回率较低、需要多个哈希表

### 2. 基于图 (Graph-based)
- **HNSW** (Hierarchical Navigable Small World)
- **NSG** (Navigating Spreading-out Graph)
- 优点: 极高的 QPS 和召回率
- 缺点: 内存开销大、增量构建复杂

### 3. 基于量化 (Quantization-based)
- **PQ** (Product Quantization)
- **SQ** (Scalar Quantization)
- **OPQ** (Optimized Product Quantization)
- 优点: 极致的内存压缩
- 缺点: 召回率略低

### 4. 基于分区 (Partition-based)
- **IVF** (Inverted File)
- **K-Means Tree**
- 优点: 可扩展性好
- 缺点: 聚类边界效应

### 5. 基于树 (Tree-based)
- **Annoy** (Approximate Nearest Neighbors Oh Yeah)
- **KD-Tree**, **Ball Tree**
- 优点: 可持久化、内存友好
- 缺点: 高维退化严重

---

## HNSW — 分层可导航小世界图

### 核心思想

HNSW 是 NSW 的多层扩展。底层是密集连接图，高层是稀疏的"高速公路"。

```
Level 3:  ·───·         (稀疏长距连接)
           \
Level 2:    ·───·───·    (中等密度)
             \ /
Level 1:  ·───·───·───·  (较密)
           \ / \ / \ /
Level 0:  ·─·─·─·─·─·─·  (全连接底层)
```

### 关键参数

| 参数 | 含义 | 推荐值 |
|------|------|--------|
| M | 每层最大连接数 | 16-64 |
| Mmax0 | 第 0 层最大连接数 | 2×M |
| efConstruction | 构建时搜索宽度 | 100-500 |
| efSearch | 查询时搜索宽度 | 16-512 |
| mL | 层级分布因子 | 1/ln(M) |

### Level 分配

节点的层级 l 服从指数分布：

```
P(l) = (1/mL)^l × (1 - 1/mL)    (归一化)
l = floor(-ln(uniform(0,1)) × mL)
```

### 贪婪搜索 (Greedy Search)

```
1. 从顶层 entry_point 开始
2. 每层执行:
   a. 维护一个候选集 W (大小 ef)
   b. 从 W 中弹出最近的未访问节点
   c. 遍历其邻居，更新 W
   d. 直到 W 中最远的距离 ≤ 候选集中最近未访问距离
3. 找到最近节点后，下降到下一层
4. 在第 0 层返回 top-k
```

### 插入算法

```
INSERT(q):
    l ← random_level()
    从顶层 entry_point 启动
    for lc = topLevel down to l+1:
        greedy_search 找到最近节点 (ef=1)
    for lc = l down to 0:
        W ← SearchLayer(q, entry_points, efConstruction)
        neighbors ← select_neighbors(W, M)
        for each n in neighbors:
            connect(q, n) 双向
            如果 n 的连接数超过 Mmax:
                剪枝最远的连接
    if l > topLevel:
        更新 entry_point = q
```

### 优缺点

| 优点 | 缺点 |
|------|------|
| 极高 QPS (百万级/秒) | 内存占用大 (~N×M×sizeof(int)) |
| 召回率高 (>99%) | 增量删除困难 |
| 无需训练 | 图构建 O(N×log N × M × efC) |
| 支持增量插入 | 参数调优需要经验 |

---

## IVF — 倒排文件索引

### 核心思想

将向量空间通过 K-Means 聚类划分为若干 Voronoi 单元，
查询时只需搜索最近几个聚类的向量。

### 算法流程

```
训练阶段:
    1. K-Means 聚类: 学习 nlist 个聚类中心
    2. 为每个向量分配最近的聚类中心

查询阶段:
    1. 计算 q 到所有聚类中心的距离
    2. 选择 nprobe 个最近聚类
    3. 在这些聚类的向量中执行精确 KNN
    4. 返回 top-k 结果
```

### 关键参数

| 参数 | 含义 | 推荐值 |
|------|------|--------|
| nlist | 聚类中心数 | sqrt(N) ~ 4*sqrt(N) |
| nprobe | 查询时探测的聚类数 | 1-32 |

### 召回率分析

召回率 ≈ 真近邻在 probed clusters 中的概率

当 nprobe = 1 时，只搜索 1/nlist 的数据，召回率低。
增加 nprobe 可线性提升召回率，但也线性增加计算量。

---

## PQ — 乘积量化

### 核心思想

将 D 维向量切分为 M 个子向量，每个子向量独立量化到 KS 个码字。
最终每个向量只需 M × log₂(KS) bits 存储。

### 子空间划分

```
原始向量: [x₁ x₂ x₃ x₄ | x₅ x₆ x₇ x₈ | ... | x_{D-3} x_{D-2} x_{D-1} x_D]
           \___________/   \___________/       \_________________________/
            子向量 1          子向量 2               子向量 M
            ↓                ↓                     ↓
           码字 k₁          码字 k₂               码字 k_M
```

### 对称距离计算 (SDC)

```
d(q, x) ≈ Σ_{m=1}^{M} || q^{(m)} - c_m(k_m(x)) ||²
```

使用预计算的查找表 (LUT)，查询复杂度 O(M × KS + M)。

### 非对称距离计算 (ADC)

```
d(q, x) ≈ Σ_{m=1}^{M} || q^{(m)} - c_m(k_m(x)) ||²

预计算: d_{m,j} = || q^{(m)} - c_{m,j} ||²  for all m, j
距离:    d(q, x) ≈ Σ_{m} d_{m, k_m(x)}
```

ADC 通常比 SDC 更精确，因为查询不经过量化。

---

## IVF-PQ — 组合索引

### 架构

```
IVF-PQ = IVF (粗量化) + PQ (细量化/压缩)

                    ┌─────────────────────┐
    查询 q ────────→│  粗量化 (IVF)       │
                    │  选择 nprobe 个聚类  │
                    └────────┬────────────┘
                             │
              ┌──────────────┼──────────────┐
              ↓              ↓              ↓
        ┌──────────┐  ┌──────────┐  ┌──────────┐
        │  聚类 1   │  │  聚类 2   │  │  聚类 3   │
        │           │  │           │  │           │
        │ PQ 编码   │  │ PQ 编码   │  │ PQ 编码   │
        │ + 残差    │  │ + 残差    │  │ + 残差    │
        └──────────┘  └──────────┘  └──────────┘
                             │
                             ↓
                    返回 top-k 结果
```

### 工作流程

1. **训练**: K-Means 聚类 → 计算残差 → PQ 训练子量化器
2. **插入**: 向量 → 最近聚类 → 残差 → PQ 编码 → 存入倒排列表
3. **查询**: 选 nprobe 聚类 → 预计算距离表 → 扫描列表 → top-k

### 内存占用

| 组件 | 内存 |
|------|------|
| 原始向量 | N × D × 4 bytes |
| IVF 索引 | N × 4 bytes (list IDs) |
| PQ 编码 | N × M bytes |
| 码本 | nlist × D × 4 + M × KS × (D/M) × 4 |

---

## LSH — 局部敏感哈希

### 核心思想

设计一族哈希函数 H，使得：

- 如果 sim(x, y) ≥ S₁，则 P[h(x)=h(y)] ≥ p₁
- 如果 sim(x, y) ≤ S₂，则 P[h(x)=h(y)] ≤ p₂

其中 p₁ > p₂，S₁ > S₂。

### 余弦相似度的 LSH: 随机超平面

```
h(v) = sign(w · v)

其中 w ~ N(0, I) 是随机高斯向量

P[h(u) = h(v)] = 1 - θ(u,v)/π

其中 θ(u,v) 是向量夹角
```

### 多表与多哈希 (AND-OR 组合)

- **AND 组合** (串联 L 个哈希): 降低碰撞概率 → 提高精度
- **OR 组合** (L 个哈希表): 提高碰撞概率 → 提高召回

使用 L 个哈希表，每个表包含 K 个哈希函数的 AND 组合。
查询时检查所有 L 个表中对应桶的向量。

---

## 性能对比与权衡

### Recall vs QPS

```
Recall
1.00 │                         ●HNSW(m=64,ef=256)
0.99 │                     ●HNSW(default)
0.95 │              ●IVF-PQ(nprobe=16)
0.90 │       ●IVF-PQ(nprobe=4)
0.85 │
0.80 │   ●LSH(L=32,K=8)
0.70 │
     └────────────────────────────────────── QPS
         10      100     1000    10000   100000
```

### 综合对比表

| 算法 | Recall | QPS | 内存 | 构建 | 增量 | 删除 |
|------|--------|-----|------|------|------|------|
| Brute Force | 1.00 | 极低 | 低 | N/A | 支持 | 支持 |
| HNSW | 0.97-0.99 | 极高 | 高 | 中等 | 支持 | 困难 |
| IVF-PQ | 0.90-0.97 | 高 | 低 | 需训练 | 支持 | 支持 |
| IVF | 0.95-0.99 | 中高 | 中 | 需训练 | 支持 | 支持 |
| LSH | 0.70-0.90 | 高 | 中 | 快 | 支持 | 支持 |
| Annoy | 0.90-0.97 | 中 | 低 | 快 | 不支持 | 不支持 |

### 选型指南

```
数据量 < 10K:      Brute Force 足够
数据量 10K-1M:     HNSW (追求 QPS) 或 IVF-PQ (追求内存)
数据量 1M-100M:    IVF-PQ 或 DiskANN
数据量 > 100M:     分布式方案 (Milvus, Weaviate)
```

---

## 工业界最佳实践

### FAISS (Meta)

Facebook AI Similarity Search 是目前最全面的向量检索库：

- **IndexFlatL2**: 精确搜索
- **IndexIVFFlat**: IVF + 精确距离
- **IndexIVFPQ**: IVF + PQ 压缩
- **IndexHNSWFlat**: HNSW + 精确距离
- **IndexHNSWPQ**: HNSW + PQ (实验性)
- **GPU 加速**: GpuIndexFlatL2, GpuIndexIVFPQ

### Milvus (Zilliz)

分布式向量数据库，支持：

- 十亿级向量规模
- 混合查询 (标量过滤 + 向量搜索)
- 多索引类型: IVF_FLAT, IVF_PQ, IVF_SQ8, HNSW, ANNOY, RHNSW_FLAT, RHNSW_PQ, RHNSW_SQ

### Annoy (Spotify)

为推荐系统优化：
- 随机投影树森林
- 内存映射支持
- 构建后不可修改
- 适合静态数据集

### 参数调优建议

1. **HNSW**: 先设 efSearch = k × 8，逐步增加直到召回满足需求
2. **IVF-PQ**: nlist = 4×sqrt(N)，nprobe 从 1 递增
3. **LSH**: 哈希表数 L 在 10-100 间，每表哈希数 K 在 4-16 间
4. **编码维度**: PQ 子空间维数建议 8-16，避免过度切分

---

## 参考论文与资源

1. Malkov & Yashunin, "Efficient and Robust Approximate Nearest Neighbor Search Using Hierarchical Navigable Small World Graphs", TPAMI 2018
2. Jegou et al., "Product Quantization for Nearest Neighbor Search", TPAMI 2011
3. Andoni & Indyk, "Near-Optimal Hashing Algorithms for Approximate Nearest Neighbor in High Dimensions", FOCS 2006
4. Johnson et al., "Billion-scale similarity search with GPUs", IEEE Big Data 2019
5. Guo et al., "Quantization based Fast Inner Product Search", AISTATS 2016
6. FAISS: https://github.com/facebookresearch/faiss
7. Milvus: https://milvus.io
8. Annoy: https://github.com/spotify/annoy
9. NMSLIB: https://github.com/nmslib/nmslib
10. Weaviate: https://weaviate.io
