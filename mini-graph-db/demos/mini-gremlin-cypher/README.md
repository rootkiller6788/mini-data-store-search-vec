# mini-gremlin-cypher — 图查询语言演示

> 参考 Cypher Query Language Reference (Neo4j, 2024), Apache TinkerPop Gremlin (v3.7.x), SPARQL 1.1 Query Language (W3C)

## 概述

图查询语言使得用户能够以声明式方式遍历和匹配图模式。本项目实现了三种主要图查询语言的子集：

| 语言 | 类型 | 设计者 | 特点 |
|------|------|--------|------|
| Cypher | 声明式 | Neo4j | ASCII 艺术语法，直观 |
| Gremlin | 函数式 | Apache TinkerPop | 链式操作，图遍历 DSL |
| SPARQL | 声明式 | W3C | RDF 三元组查询，语义网 |

## Cypher — Neo4j 查询语言

### 设计哲学

Cypher 的设计理念是"让简单的事情简单，复杂的事情可能"（"Make the simple things easy, and the complex things possible"）。

### 基本语法元素

#### 节点模式 (Node Pattern)
```
(variable:Label1:Label2 {key: "value", key2: 42})
```
- `variable`: 变量名，用于后续引用
- `:Label`: 节点标签（类型）
- `{properties}`: 属性过滤条件

#### 边模式 (Edge Pattern)
```
-[:TYPE {key: value}]->
<-[variable:TYPE]-
-[variable]-          // 无向
```

#### 方向
| 语法 | 含义 |
|------|------|
| `-->` | 向右（出边） |
| `<--` | 向左（入边） |
| `--` | 无向 |

### 本实现支持的语法

```
(a:Person)-[:KNOWS]->(b:Person)
(a:Admin {role: "super"})-[:MANAGES]->(b)
```

#### 解析器 (`src/cypher_like.c`)
```c
bool cypher_parse(const char *query_string, QueryPattern *pattern);
```

解析步骤：
1. 识别左括号 `(`
2. 解析变量名
3. 解析标签（`:` 前缀）
4. 解析属性过滤 `{key:value}`
5. 识别右括号 `)`
6. 解析边 `-[type]->`
7. 重复步骤 1-5 解析右侧节点

#### 模式匹配 (`src/cypher_like.c`)
```c
QueryResult cypher_match(PropertyGraph *g, QueryPattern *pattern);
```

匹配策略：
1. 遍历所有节点，筛选匹配左侧模式的节点
2. 对每个匹配的左侧节点，检查出边
3. 筛选边类型匹配的边
4. 验证目标节点匹配右侧模式
5. 收集绑定变量

#### 结果格式
每行包含变量到节点/边的绑定：
```
Row 1: a=1  b=2
Row 2: a=1  b=3
```

## Apache TinkerPop Gremlin

### 设计哲学

Gremlin 是一种函数式数据流语言，图的遍历被表达为一系列组合的步骤（steps）。

### 核心概念：Traversal

Gremlin 遍历是惰性求值的——步骤被连接在一起但直到遍历被迭代时才执行。

### Gremlin 步骤（Steps）对照

#### 变换步骤 (Transform)
| Gremlin 步骤 | 功能 | 对应 SQL |
|-------------|------|----------|
| `V()` | 所有顶点 | `SELECT * FROM nodes` |
| `E()` | 所有边 | `SELECT * FROM edges` |
| `out(label)` | 沿出边遍历 | JOIN |
| `in(label)` | 沿入边遍历 | JOIN |
| `both(label)` | 双向遍历 | JOIN (无向) |
| `outE(label)` | 获取出边 | 边访问 |
| `inV()` / `outV()` | 获取边的端点 | 关联 |
| `values(prop)` | 提取属性值 | 列投影 |
| `path()` | 返回完整路径 | 轨迹 |

#### 过滤步骤 (Filter)
| 步骤 | 功能 |
|------|------|
| `has(label, key, value)` | 属性/标签过滤 |
| `hasLabel(label)` | 标签过滤 |
| `hasNot(key)` | 不存在某属性 |
| `is(value)` | 值比较 |
| `where(traversal)` | 子遍历过滤 |
| `dedup()` | 去重 |
| `limit(n)` | 限制结果数 |
| `range(m, n)` | 范围分页 |

#### 副作用步骤 (Side Effect)
| 步骤 | 功能 |
|------|------|
| `as(name)` | 命名当前元素 |
| `select(name)` | 检索命名元素 |
| `aggregate(name)` | 收集所有元素 |
| `group()` / `groupCount()` | 分组 / 计数 |
| `order()` / `by()` | 排序 |
| `profile()` | 性能分析 |

### Gremlin 与 Cypher 对比

```
Cypher:
MATCH (a:Person)-[:KNOWS]->(b:Person)
RETURN a.name, b.name

Gremlin:
g.V().hasLabel("Person").as("a")
 .out("KNOWS").as("b")
 .select("a","b")
 .by("name")
```

### 遍历策略

Gremlin 支持两种遍历策略：

| 策略 | 说明 | 使用场景 |
|------|------|----------|
| **深度优先** | O(log V) 内存 | 查找单一路径 |
| **广度优先** | O(V) 内存 | 查找所有最短路径 |

通过 `withStrategies()` 配置。

## SPARQL — 语义网查询

### RDF 数据模型

SPARQL 基于 RDF 三元组（主语, 谓词, 宾语）：

```
<Alice> <knows> <Bob> .
<Alice> <age> "30"^^xsd:int .
```

### 基本图模式 (BGP)

```
SELECT ?name ?friend
WHERE {
  ?person  foaf:name  ?name .
  ?person  foaf:knows ?friend .
}
```

### SPARQL vs Cypher 对比

| 方面 | SPARQL | Cypher |
|------|--------|--------|
| 数据模型 | RDF 三元组 | 属性图 |
| 标识 | URI / IRI | 内部 ID |
| 属性 | 谓词-宾语对 | 键值对 |
| 模式匹配 | 三元组模式 | 图模式 / ASCII 艺术 |
| 外部数据 | SERVICE / FROM | 仅本地图 |
| 推理 | OWL / RDFS 推理支持 | 无内置推理 |
| 标准 | W3C 推荐标准 | 开放标准 (openCypher) |

## 模式匹配实现原理

### 朴素模式匹配

最简单的实现：遍历所有可能的节点组合，验证是否满足模式。

```
for each node A in graph:
    if A does not match node_pattern[0]: continue
    for each edge E from A:
        if E does not match edge_pattern[0]: continue
        B = target of E
        if B does not match node_pattern[1]: continue
        emit binding (A, B)
```

复杂度: O(V · d_max)，其中 d_max 为最大出度。

### 基于索引的优化

1. **标签索引** (`graph_index.h`):
   - 通过标签缩小起始节点候选集
   - 将 O(V) 扫描降为 O(|labeled_nodes|)

2. **边类型索引**:
   - 通过边类型快速定位相关边
   - 避免扫描所有出边

3. **属性索引**:
   - 加速属性过滤条件
   - 等值查询 O(1)

### 优化效果
| 策略 | 扫描节点数 | 适用场景 |
|------|----------|---------|
| 朴素 | V | 小图 |
| 标签索引 | \|labeled\| | 有标签的模式 |
| 标签+边索引 | \|labeled\| · d_filtered | 有类型边 |
| 全索引 | near O(result size) | 最优点查询 |

## 查询优化技术

### 1. 模式重排序
将选择性最高的条件（产生最少中间结果）放在前面执行。

### 2. 预计算聚合
常用统计量（度数、PageRank 等）预先计算并存储为节点属性。

### 3. 物化路径
对于频繁查询的长路径，可预先物化并索引。

### 4. 顶点-中心索引 (Vertex-Centric Index)
每个节点维护按边类型分组的邻接表，加速带标签的遍历。

## 扩展：openCypher 与 Graph Query Language (GQL)

### openCypher (OCM)
- Neo4j 在 2015 年开源了 Cypher 规范
- 目前有 10+ 种数据库实现
- 标准还在发展中

### GQL (ISO/IEC 39075:2024)
- ISO 标准图查询语言
- 融合 Cypher 和 SQL 特征
- 属性图数据模型
- 2024 年发布

## 图查询语言选择指南

| 需求 | 推荐 |
|------|------|
| Neo4j 生态 | Cypher |
| Apache 生态 (JanusGraph, Neptune) | Gremlin |
| 语义网 / 知识图谱 | SPARQL |
| 通用标准 | openCypher / GQL |
| PostgreSQL (AGE) | openCypher |
| 简单图匹配 | 自定义 DSL |

## 本项目的 query 示例

```
# 查找所有 KNOWS 关系
(a:Person)-[:KNOWS]->(b)

# 查找管理员之间的社交关系
(a:Admin)-[:KNOWS]->(b:Admin)

# 查找关注关系
(a:Person)-[:FOLLOWS]->(b:Person)

# 查找单节点
(a:Person)

# 带属性过滤
(a:Person {role: "admin"})-[:KNOWS]->(b)
```

## 未来改进

1. **WHERE 子句**: 支持布尔表达式过滤
2. **RETURN 投影**: 指定返回的变量和属性
3. **路径模式**: 可变长度路径 `()-[:KNOWS*2..4]->()`
4. **聚合函数**: COUNT, SUM, AVG
5. **OPTIONAL MATCH**: 可选模式（类似 LEFT JOIN）
6. **CREATE/MERGE/DELETE**: 写操作
7. **参数化查询**: 防止注入，性能优化

## 参考资料
- *Graph Databases* (Robinson, Webber, Eifrem; O'Reilly 2015) — 第 3 章: 数据建模与查询
- *Neo4j Cypher Manual* (Neo4j, Inc.) — 官方 Cypher 参考
- *Apache TinkerPop Documentation* (Apache Software Foundation) — Gremlin 遍历语言
- *SPARQL 1.1 Query Language* (W3C Recommendation, 2013) — SPARQL 规范
- *openCypher Specification* (openCypher Implementers Group) — 开放标准
- *Making Sense of GQL* (Alastair Green, 2024) — GQL 标准解读
