# mini-graph-db — 图数据库 (C 语言实现)

> 参考 Neo4j Internals, Graph Databases (O'Reilly), Apache TinkerPop Gremlin

## 概述

mini-graph-db 是一个用 C99 实现的轻量级图数据库内核，包含属性图存储、图遍历、图算法、类 Cypher 查询语言和图索引结构。

```
mini-graph-db/
├── include/
│   ├── property_graph.h      # 属性图数据模型
│   ├── graph_traversal.h     # 图遍历 (BFS/DFS/Dijkstra)
│   ├── graph_algo.h          # 图算法 (PageRank等)
│   ├── cypher_like.h         # 类 Cypher 查询语言
│   └── graph_index.h         # 图索引结构
├── src/
│   ├── property_graph.c      # 属性图实现
│   ├── graph_traversal.c     # 图遍历实现
│   ├── graph_algo.c          # 图算法实现
│   ├── cypher_like.c         # 查询解析/匹配实现
│   └── graph_index.c         # 索引实现
├── examples/
│   ├── graph_traversal_demo.c # 遍历演示 (社交网络)
│   ├── pagerank_demo.c        # PageRank 演示 (网页图)
│   └── cypher_demo.c          # Cypher 查询演示
├── demos/
│   ├── mini-graph-algorithms/ # 图算法详解
│   └── mini-gremlin-cypher/   # 图查询语言详解
├── docs/
│   ├── course-alignment.md
│   └── graph-database-internals.md
├── Makefile
└── README.md
```

## 特性

- **属性图**: 节点带多个标签和键值属性，边带类型和方向
- **图遍历**: BFS、DFS、最短路径、Dijkstra 加权最短路径、全路径搜索
- **图算法**: PageRank、标签传播（社区检测）、连通分量、拓扑排序、环检测
- **查询**: 类 Cypher 声明式图模式匹配 `(a:Person)-[:KNOWS]->(b)`
- **索引**: 标签索引、边类型索引、属性索引、空间网格索引

## 快速开始

### 构建

```bash
make
```

### 运行示例

```bash
make run-traversal    # 社交网络 BFS/DFS/Dijkstra 演示
make run-pagerank     # PageRank 网页排名演示
make run-cypher       # Cypher 模式匹配演示
```

### 清理

```bash
make clean
```

## 数据模型

### Node
```
Node {
    id: int64                # 唯一标识
    labels: string[4]        # 最多 4 个标签
    properties: Property[16] # 最多 16 个键值属性
}
```

### Edge
```
Edge {
    id: int64                # 唯一标识
    type: string             # 边类型 (如 "KNOWS")
    from_node: int64         # 源节点
    to_node: int64           # 目标节点
    directed: bool           # 是否有向
    properties: Property[8]  # 最多 8 个属性
}
```

## 使用示例

### 创建图并添加节点

```c
PropertyGraph *g = graph_create();

Node *alice = graph_create_node(g);
graph_node_add_label(alice, "Person");
graph_node_set_property(alice, "name", "Alice");
graph_node_set_property(alice, "age", "30");

Node *bob = graph_create_node(g);
graph_node_add_label(bob, "Person");
graph_node_set_property(bob, "name", "Bob");
```

### 创建边

```c
Edge *e = graph_create_edge(g, alice->id, bob->id, "KNOWS", true);
graph_edge_set_property(e, "since", "2020");
```

### BFS 遍历

```c
PathResult bfs = traverse_bfs(g, alice->id, bob->id);
path_result_print(&bfs);
```

### Dijkstra 加权最短路径

```c
double weight_func(Edge *e) { /* return edge weight */ }
PathResult path = traverse_dijkstra(g, alice->id, bob->id, weight_func);
```

### PageRank

```c
pagerank_print_top(g, 10);
```

### Cypher 查询

```c
QueryPattern pattern;
cypher_parse("(a:Person)-[:KNOWS]->(b)", &pattern);
QueryResult result = cypher_match(g, &pattern);
cypher_print_results(&result);
```

## 容量限制

| 资源 | 限制 | 宏 |
|------|------|---|
| 节点 | 4096 | MAX_NODES |
| 边 | 16384 | MAX_EDGES |
| 节点标签 | 4 | MAX_NODE_LABELS |
| 节点属性 | 16 | MAX_NODE_PROPERTIES |
| 边属性 | 8 | MAX_EDGE_PROPERTIES |
| 路径长度 | 256 | MAX_PATH_LENGTH |

## 算法复杂度

| 算法 | 时间 | 空间 |
|------|------|------|
| BFS/DFS | O(V+E) | O(V) |
| Dijkstra | O(V²) | O(V) |
| PageRank | O(k·E) | O(V) |
| Label Propagation | O(k·E) | O(V) |
| Connected Components | O(E·α(V)) | O(V) |
| Topological Sort | O(V+E) | O(V) |
| Cycle Detection | O(V+E) | O(V) |

## 参考资料

- *Graph Databases* (Ian Robinson, Jim Webber, Emil Eifrem) — O'Reilly, 第 2 版
- *Neo4j Internals* — 存储引擎与查询引擎架构
- *Apache TinkerPop* — Gremlin 图遍历语言与虚拟机
- *Introduction to Algorithms* (CLRS) — 第三版, 第 VI 部分: 图算法

## 许可

MIT License
