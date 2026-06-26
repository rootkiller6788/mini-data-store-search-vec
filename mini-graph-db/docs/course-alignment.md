# 课程对齐文档 — mini-graph-db

> 对比大学课程与工业实践的图数据库知识体系

## 数据结构 (Data Structures)

### 课程内容
- 数组、链表、栈、队列
- 哈希表、散列函数、冲突处理
- 图的基本表示: 邻接矩阵 vs 邻接表

### mini-graph-db 对应实现

| 数据结构 | 用途 | 文件 |
|---------|------|------|
| 哈希表 (链地址法) | Node ID → Node Index 映射 | `src/property_graph.c:hash_int64()` |
| 邻接表 (链表) | 节点的出边列表 | `property_graph.h:AdjList` |
| 循环队列 (数组) | BFS 遍历 | `src/graph_traversal.c:queue[]` |
| 显式栈 (数组) | DFS 遍历 | `src/graph_traversal.c:stack[]` |
| 并查集 (Union-Find) | 连通分量检测 | `src/graph_algo.c:uf_find/uf_union` |
| 优先队列 (简单数组) | Dijkstra 最短路径 | `src/graph_traversal.c:extract_min()` |

### 知识点映射
- **CS201 哈希表**: 链地址法哈希用于 O(1) 节点查找
- **CS202 图表示**: 邻接表比矩阵节省空间 O(V+E) vs O(V²)
- **CS301 并查集**: 带路径压缩和按秩合并

## 算法设计与分析 (Design & Analysis of Algorithms)

### 课程内容
- 图遍历算法
- 最短路径算法
- 贪心算法
- 动态规划
- NP 完全性

### mini-graph-db 对应实现

| 算法 | 复杂度 | 类型 | 文件位置 |
|------|--------|------|---------|
| BFS | O(V+E) | 图遍历 | `graph_traversal.c:traverse_bfs()` |
| DFS | O(V+E) | 图遍历 | `graph_traversal.c:traverse_dfs()` |
| Dijkstra | O(V²) | 贪心 | `graph_traversal.c:traverse_dijkstra()` |
| PageRank | O(k·E) | 迭代 | `graph_algo.c:pagerank()` |
| Kahn 拓扑排序 | O(V+E) | BFS 变体 | `graph_algo.c:topological_sort()` |
| 三色 DFS 环检测 | O(V+E) | DFS 变体 | `graph_algo.c:cycle_detection()` |

### 算法正确性分析

#### BFS 最短路径正确性 (无权图)
- **归纳假设**: 第 k 层节点在第 k 轮被访问
- **基础**: 源节点在第 0 轮访问，dist=0
- **归纳**: 如果所有距离 ≤k 的节点已被访问，则距离 k+1 的节点在 k+1 轮被访问
- **结论**: BFS 按距离升序访问节点，保证最短路径

#### Dijkstra 正确性
- **贪心选择性质**: 每次选择 dist 最小且未访问的节点
- **证明思路** (反证):
  - 假设算法第一次犯错：u 是最短路径估计错误的已处理节点
  - 存在通过某个未处理节点 v 的更短路径
  - 但 dist[v] ≥ dist[u]（因为选择了 u）
  - 因此 dist[u] + w(v,u) > dist[v] ≥ dist[u]，矛盾

#### PageRank 收敛性
- 阻尼因子 d < 1 确保转移矩阵的谱半径 ρ < 1
- 幂迭代法 (Power Iteration) 线性收敛
- 收敛速率 ∝ d^k

## 数据库系统 (Database Systems)

### 课程内容
- 数据模型 (关系、文档、图)
- 索引结构 (B+树、哈希)
- 查询处理与优化
- 事务与并发控制

### mini-graph-db 对应实现

| 概念 | 实现 | 说明 |
|------|------|------|
| **数据模型** | 属性图 | 节点 + 边 + 属性 (标签属性图) |
| **存储引擎** | 数组 + 哈希索引 | 固定容量，内存存储 |
| **索引** | 标签/类型/属性索引 | 哈希桶索引 O(1) 等值查询 |
| **查询处理** | 模式匹配 | 遍历 + 过滤 |
| **查询优化** | 索引辅助 | 标签索引缩小候选集 |
| **查询语言** | 类 Cypher | 声明式图模式匹配 |

### 图数据库 vs 关系数据库

| 操作 | SQL (关系) | Cypher (图) |
|------|-----------|-------------|
| 查找朋友 | `SELECT * FROM friends JOIN users ...` | `(a)-[:KNOWS]->(b)` |
| 2 跳朋友 | 多次自连接 | `()-[:KNOWS*2]->()` |
| 最短路径 | 递归 CTE / BFS 在应用层 | 内置 `shortestPath()` |
| PageRank | 外部计算 | 内置算法 |

### 为什么图数据库更适合关联数据

**JOIN 爆炸问题**: 在关系数据库中，多跳遍历需要多次 JOIN。
```sql
-- 3 跳朋友查询在 SQL 中
SELECT f3.* FROM users u
JOIN friends f1 ON u.id = f1.user_id
JOIN friends f2 ON f1.friend_id = f2.user_id
JOIN friends f3 ON f2.friend_id = f3.user_id
WHERE u.name = 'Alice';
-- 复杂度: O(N^d) where d = 跳数
```

在图数据库中，邻接表使得每跳 O(d_avg)，总复杂度 O(d_avg · hops)：
```
(a:Person {name:"Alice"})-[:KNOWS*3]->(friend)
-- 复杂度: O(d_avg³) 且索引辅助
```

## 操作系统 (Operating Systems)

### 课程内容
- 内存管理
- 文件系统
- I/O 调度

### mini-graph-db 对应设计

| 概念 | 当前实现 | 工业实现 |
|------|---------|---------|
| 内存分配 | `malloc/calloc` | 自定义内存池 |
| 存储持久化 | 无 (仅内存) | WAL + SSTable |
| 缓存管理 | 无显式 | LRU 页面缓存 |
| 文件格式 | N/A | 固定大小记录文件 |

**未来扩展**: 将图持久化到磁盘，需要实现：
1. **Write-Ahead Log (WAL)**: 先写日志，再写数据
2. **Checkpoint**: 定期将内存快照写入磁盘
3. **Recovery**: 崩溃后从 WAL 恢复

## 计算机网络 (Computer Networks)

### 课程内容
- 路由协议
- 链路状态 vs 距离向量

### 图算法在网络中的应用

| 网络概念 | 图算法 | 说明 |
|---------|--------|------|
| **OSPF 路由** | Dijkstra | 链路状态路由协议 |
| **BGP 路由** | 路径向量 | AS 级别路由 |
| **组播路由** | 最小生成树 | Steiner 树近似 |
| **CDN 放置** | 中心性度量 | 介数中心性 |
| **P2P 查找** | 分布式哈希表 | Chord, Kademlia |

## 人工智能 (Artificial Intelligence)

### 课程内容
- 搜索算法
- 知识表示
- 推理

### graph-db 在 AI 中的应用

| AI 概念 | 图数据库用法 |
|---------|------------|
| **知识图谱** | RDF 三元组存储与 SPARQL 查询 |
| **推荐系统** | 协同过滤 → 图上的随机游走 |
| **路径规划** | A* 搜索 → 带启发式的 Dijkstra |
| **社交网络分析** | 社区检测、影响力传播 |
| **本体推理** | 基于规则的推理、传递性闭合 |

## 软件工程 (Software Engineering)

### 课程内容
- 模块化设计
- 接口与实现分离
- 测试与文档

### mini-graph-db 软件工程实践

| 实践 | 实现 |
|------|------|
| **头文件 API** | include/ 目录下的公接口 |
| **源文件实现** | src/ 目录下的实现细节 |
| **示例程序** | examples/ 目录下的可运行演示 |
| **文档** | demos/ 目录下的详细教程 |
| **构建系统** | Makefile 统一构建 |

## 推荐学习路径

1. **入门**: 阅读 README.md，运行示例程序
2. **数据结构**: 阅读 `property_graph.h` 和 `.c`，理解哈希表和邻接表
3. **图算法**: 阅读 `graph_algo.c`，对比 CLRS 教材
4. **数据库**: 阅读 `graph_index.h` 和 `cypher_like.c`，理解索引和查询优化
5. **系统设计**: 阅读 `graph-database-internals.md`，了解工业级图数据库架构
6. **项目扩展**: 实现持久化存储、查询优化器、分布式分片
