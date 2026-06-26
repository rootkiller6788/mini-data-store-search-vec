# mini-graph-algorithms — 图算法演示

> 参考 Introduction to Algorithms (CLRS), Graph Theory (Bondy & Murty), Network Science (Barabási)

## 概述

图算法是图数据库和分析系统的核心。本项目演示了以下图算法的 C99 实现：

| 算法 | 用途 | 复杂度 |
|------|------|--------|
| BFS | 广度优先搜索 | O(V+E) |
| DFS | 深度优先搜索 | O(V+E) |
| Dijkstra | 加权最短路径 | O(V²) |
| PageRank | 网页排名 | O(k·E) |
| Label Propagation | 社区检测 | O(k·E) |
| Connected Components | 连通分量 | O(E·α(V)) |
| Topological Sort | 拓扑排序 | O(V+E) |
| Cycle Detection | 环检测 | O(V+E) |

## BFS (Breadth-First Search)

广度优先搜索按层级遍历图，适合查找最短路径（无权图）。

### 算法步骤
1. 将起始节点入队，标记已访问
2. 当队列非空，出队一个节点
3. 访问该节点的所有未访问邻居，入队并标记
4. 重复直到队列为空或找到目标

### 数据结构
- FIFO 队列
- visited 数组
- parent 数组（用于路径回溯）

### 实现细节 (`src/graph_traversal.c`)
```c
PathResult traverse_bfs(PropertyGraph *g, int64_t start, int64_t target);
```

BFS 保证在无权图中找到从 start 到 target 的最短路径（最少边数）。使用基于数组的队列，O(V+E) 时间复杂度。

### BFS vs DFS 选择
| 场景 | 推荐 |
|------|------|
| 最短路径 | BFS |
| 全图遍历 | 任意 |
| 拓扑排序 | DFS |
| 环检测 | DFS |
| 社交网络 N 跳邻居 | BFS |

## DFS (Depth-First Search)

深度优先搜索沿一条路径尽可能深入，然后回溯。

### 三种状态
- **0 (WHITE)**: 未访问
- **1 (GRAY)**: 访问中（递归栈中）
- **2 (BLACK)**: 已完成

三色 DFS 是环检测和拓扑排序的基础。

### 栈实现 (`src/graph_traversal.c`)
```c
PathResult traverse_dfs(PropertyGraph *g, int64_t start, int64_t target);
```

使用显式栈而非递归，避免大图的栈溢出。

## Dijkstra 最短路径

加权图中的单源最短路径算法。

### 算法
```
for each vertex v:
    dist[v] = INFINITY
dist[source] = 0

while unvisited vertices remain:
    u = vertex with minimum dist[u]
    mark u as visited
    for each neighbor v of u:
        alt = dist[u] + weight(u, v)
        if alt < dist[v]:
            dist[v] = alt
```

### 权重函数
用户可提供自定义权重函数：
```c
double (*weight_func)(Edge *e);
```
默认为单位权重（等价于 BFS 最短路径）。

### 当前局限
- 使用简单 O(V²) 选择最小值（适合小图）
- 大图可升级为二叉堆优化至 O(E·log V)

## PageRank

Google 的网页排名算法，由 Larry Page 和 Sergey Brin 在 1998 年提出。

### 迭代公式

```
PR(p) = (1-d)/N + d * Σ(PR(q)/L(q))
```

其中:
- N 为节点总数
- d 为阻尼因子（通常 0.85）
- L(q) 为节点 q 的出度
- 求和遍历所有指向 p 的节点 q

### 随机冲浪解释 (Random Surfer Model)

用户以概率 d 沿着链接前进，以概率 (1-d) 随机跳转到任意页面。

### 收敛条件
- 最大迭代次数: 100
- 收敛阈值: 1e-6（PageRank 值变化总和）

### 阻尼因子的影响
| d 值 | 效果 |
|------|------|
| 0.85 | 标准值，平衡链接和随机跳转 |
| 0.99 | 更依赖链接结构 |
| 0.50 | 更多随机性 |

### 实现 (`src/graph_algo.c`)
```c
int pagerank(PropertyGraph *g, RankedNode *results, int max_results,
             double damping, int max_iter, double epsilon);
```

## Label Propagation (标签传播)

半监督社区检测算法，Raghavan 等人在 2007 年提出。

### 算法
1. 每个节点初始化为唯一标签（其 ID）
2. 迭代：每个节点选择邻居中出现最多的标签
3. 平局时选择最小编号标签（确定性）
4. 收敛或达到最大迭代次数

### 特点
- **近线性时间**: O(k·E)，k 为迭代次数
- **无需先验知识**: 无需指定社区数量
- **随机性处理**: 平局时选择最小编号标签保证确定性

### 应用
- 社交网络社区发现
- 蛋白质相互作用网络
- 引文网络主题聚类

## Connected Components (连通分量)

使用 Union-Find（并查集）数据结构。

### Union-Find 操作
- **Find**: 带路径压缩，接近 O(1) 均摊
- **Union**: 按秩合并，O(α(V)) 均摊

### 实现
```c
int connected_components(PropertyGraph *g, int64_t *component_of,
                         int *component_count);
```

支持无向图和有向图的弱连通分量检测。

### α(n) 函数
反阿克曼函数，对于任何实际输入 α(n) ≤ 4。实际上 Union-Find 可以认为是线性时间。

## Topological Sort (拓扑排序)

仅适用于有向无环图 (DAG)。基于 Kahn 算法。

### 算法 (Kahn)
1. 计算每个节点的入度
2. 将所有入度为 0 的节点入队
3. 出队节点，将其加入结果
4. 将该节点的所有出边目标入度减 1
5. 如果入度变为 0，入队
6. 重复直到队列为空

### 判断环
如果排序结果中的节点数 != 总节点数，则图中存在环。

### 应用
- 任务调度
- 编译依赖解析
- 课程先修关系
- 构建系统 (make, ninja)

## Cycle Detection (环检测)

使用三色 DFS（WHITE/GRAY/BLACK）。

### 检测原理
DFS 过程中遇到 GRAY（访问中）节点表示存在环（后向边）。

### 环路径回溯
通过 parent 数组回溯，构建环的节点序列。

### 实现
```c
bool cycle_detection(PropertyGraph *g, int64_t *cycle, int *cycle_len);
```

## 图遍历可视化示例

```
     1(Alice)
    /  |  \
   2   3   4
  /|   |\  |
 5 6   7 8 9
        \|/
        10
```

BFS 顺序: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10
DFS 顺序: 1 → 2 → 5 → 6 → 3 → 7 → 8 → 10 → 9 → 4

## 性能特征

| 算法 | 时间复杂度 | 空间复杂度 | 适用图大小 |
|------|-----------|-----------|-----------|
| BFS/DFS | O(V+E) | O(V) | up to 4096 nodes |
| Dijkstra | O(V²) | O(V) | up to 4096 nodes |
| PageRank | O(k·E) | O(V) | up to 4096 nodes |
| Label Prop. | O(k·E) | O(V) | up to 4096 nodes |
| Conn. Comp. | O(E·α(V)) | O(V) | up to 4096 nodes |
| Topo Sort | O(V+E) | O(V) | DAG only |
| Cycle Det. | O(V+E) | O(V) | up to 4096 nodes |

注意: 当前实现针对教育目的优化，最大支持 4096 个节点和 16384 条边。

## 高级图算法（扩展方向）

以下算法适合作为未来扩展：

### 中心性度量
- **介数中心性** (Betweenness Centrality): 节点出现在最短路径中的频率
- **紧密中心性** (Closeness Centrality): 到所有其他节点的平均最短距离
- **特征向量中心性** (Eigenvector Centrality): 连接重要节点的节点更重要

### 社群检测
- **Louvain 算法**: 基于模块度优化的层次聚类
- **Girvan-Newman**: 基于边介数的分裂算法
- **Spectral Clustering**: 基于拉普拉斯矩阵特征值

### 路径与匹配
- **A\* 搜索**: 带启发式函数的 Dijkstra
- **K-最短路径** (Yen 算法): 前 K 条最短路径
- **最大流/最小割**: Ford-Fulkerson, Edmonds-Karp
- **二分图匹配**: Hopcroft-Karp

### 图嵌入
- **Node2Vec**: 基于随机游走的节点嵌入
- **DeepWalk**: 社交网络表示学习

## 参考资料
- *Introduction to Algorithms (CLRS)*, 第 3 版 — 22-26 章: 图算法
- *Graph Theory* (Bondy & Murty, 2008) — 图论基础
- *Network Science* (Barabási, 2016) — 网络科学
- *The PageRank Citation Ranking* (Page, Brin 等, 1999) — PageRank 原始论文
- *Near linear time algorithm to detect community structures* (Raghavan 等, 2007) — 标签传播

## 构建

```bash
make
make run-tests
```
