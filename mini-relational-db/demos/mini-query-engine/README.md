# mini-query-engine — Query Engine: SQL -> Parse -> Plan -> Optimize -> Execute (Volcano)

> 参考 CMU 15-445 Database Systems, Volcano — An Extensible and Parallel Query Evaluation System (Graefe, 1994)

## Overview

A query engine that compiles SQL text into an executable plan tree using the Volcano iterator model. The pipeline has four stages:

```
SQL Text --> Parser --> Logical Plan --> Optimizer --> Physical Executor --> Result Tuples
```

### 1. SQL Parser (`include/sql_parser.h`, `src/sql_parser.c`)

A hand-written recursive descent parser supporting:

| Statement      | Syntax |
|---------------|--------|
| SELECT        | `SELECT col1,col2 FROM table WHERE col1>10 ORDER BY col1` |
| INSERT        | `INSERT INTO table VALUES (1, 'xyz', 99)` |
| CREATE TABLE  | `CREATE TABLE t (id INT, name VARCHAR(50), note TEXT)` |
| DROP TABLE    | `DROP TABLE t` |

The tokenizer skips whitespace, handles commas, identifiers, quoted string values, and comparison operators (`=`, `!=`, `<`, `<=`, `>`, `>=`). All parsed statements are stored in the `SQLStmt` struct.

Key function: `sql_parse(sql, stmt)` dispatches to `sql_parse_select`, `sql_parse_insert`, `sql_parse_create`, or `sql_parse_drop` based on the first keyword.

### 2. Query Optimizer (`include/query_optimizer.h`, `src/query_optimizer.c`)

Read - a cost-based optimizer based on the classic PostgreSQL planner model.

**Plan Nodes** represent physical algebra operators:

| Node Type            | Meaning |
|---------------------|---------|
| `SEQ_SCAN`          | Sequential scan of a table |
| `INDEX_SCAN`        | Index-based lookup |
| `HASH_JOIN`         | Hash table join |
| `NESTED_LOOP_JOIN`  | Cartesian product + filter |
| `SORT_MERGE_JOIN`   | Sort both sides, merge |
| `FILTER`            | Apply WHERE predicate |
| `PROJECTION`        | Select output columns |
| `SORT`              | Sort by ORDER BY key |
| `HASH_AGG`          | Hash-based aggregation |

**Cost Model:**

```
SeqScan cost       = pages * 0.1
Filter selectivity = EQ:1%, NE:99%, GT/LT:33%
Sort cost          = N * log2(N) * 0.2
HashJoin cost      = (outer + inner) * 0.5
NestedLoop cost    = outer * inner * 0.05
SortMerge cost     = sort(outer) + sort(inner) + merge_scan
```

**Dynamic Programming Optimizer** (`opt_choose_best`):
For multi-table joins, the DP optimizer enumerates all join shapes bottom-up. For each subset of tables, it tries all join algorithms (hash, nested-loop, sort-merge) and picks the lowest estimated cost. This is a simplified version of the classic System R optimizer (Selinger et al., 1979).

### 3. Volcano Executor (`include/volcano_executor.h`, `src/volcano_executor.c`)

Based on the Volcano iterator model. Each executor implements three functions:

```
open()  --> initialize the operator
next()  --> return one output tuple, or EOF
close() --> release resources
```

**Executor Types:**

- `SeqScanExecutor` — iterates table rows, skipping deleted
- `FilterExecutor` — passes through tuples matching WHERE predicate
- `ProjectExecutor` — selects a subset of columns (projection push-down)
- `SortExecutor` — materializes all tuples, sorts them, returns sorted
- `HashJoinExecutor` — build hash table from inner, probe with outer
- `NestedLoopJoinExecutor` — double loop over outer and inner
- `SortMergeJoinExecutor` — materialize both, sort, merge
- `AggExecutor` — COUNT(*) aggregate over groups

**Building from Plan** (`exec_build_plan`): Recursively walks the `PlanNode` tree and creates the corresponding executor pipeline.

### 4. Example: `examples/sql_demo.c`

Demonstrates the full pipeline:
1. Parse `SELECT name,age FROM users WHERE age>20 ORDER BY age`
2. Create table with schema (id INT, name VARCHAR(50), age INT)
3. Insert 4 rows
4. Build executor pipeline: SeqScan -> Filter(age>22) -> Project(name,age) -> Sort(age)
5. Iterate with `exec_next()` printing results

**Output:**
```
=== Mini Relational DB - SQL Demo ===

--- Parser Tests ---
Parsed SELECT: SELECT name, age FROM users WHERE age > 20 ORDER BY age
Parsed DDL: CREATE TABLE users (id INT, name VARCHAR(50), age INT);
Parsed DML: INSERT INTO users VALUES (1, 'Alice', 30);
Parsed DROP: DROP TABLE users;

--- Table Operations ---
=== users (3 columns, 4 rows) ===
id           | name         | age
-------------+--------------+------------
1            | Alice        | 30
2            | Bob          | 25
3            | Carol        | 35
4            | Dave         | 22

--- Volcano Executor Pipeline ---
SELECT name,age FROM users WHERE age>22 ORDER BY age:
  Alice | 30
  Carol | 35
  Bob   | 25

=== Done ===
```

## Building

```bash
make && make examples
./build/sql_demo
```

## Key Design Decisions

1. **Volcano pull model** — each operator calls `next()` on its child; data flows bottom-up on demand
2. **Open-Next-Close protocol** — consistent across all executor types; facilitates composability
3. **Cost-based optimization** — statistical estimates drive plan choice rather than heuristics
4. **Pure C99** — zero external dependencies; libc+libm only
5. **Modular headers** — each subsystem in its own `include/` header with single implementation in `src/`

## References

- Graefe, G. (1994). "Volcano — An Extensible and Parallel Query Evaluation System". *IEEE TKDE*.
- Selinger, P. G., et al. (1979). "Access Path Selection in a Relational Database Management System". *SIGMOD*.
- CMU 15-445/645 Database Systems, Lecture 11-14: Query Execution, Query Optimization.
