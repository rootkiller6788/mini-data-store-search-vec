# mini-relational-db — 关系型数据库 (C 语言实现)

> 参考 CMU 15-445, PostgreSQL Internals

A miniature relational database engine written in C99. Implements a full SQL query processing pipeline: parser, optimizer, and volcano-style executor with multiple join algorithms.

## Structure

```
mini-relational-db/
├── include/
│   ├── sql_parser.h          # SQL AST types and parser API
│   ├── query_optimizer.h     # Cost-based optimizer, plan nodes
│   ├── volcano_executor.h    # Volcano iterator model executor
│   ├── join_algorithms.h     # Hash, Nested-Loop, Sort-Merge, GRACE joins
│   └── row_store.h           # Row-based table storage
├── src/
│   ├── sql_parser.c          # Recursive descent SQL parser (190+ lines)
│   ├── query_optimizer.c     # DP optimizer with cost model (230+ lines)
│   ├── volcano_executor.c    # Volcano executors (340+ lines)
│   ├── join_algorithms.c     # Join implementations (210+ lines)
│   └── row_store.c           # Table create/insert/scan/sort (150+ lines)
├── examples/
│   ├── sql_demo.c            # Full pipeline: parse -> table -> execute
│   ├── join_demo.c           # Compare all join algorithms on users+orders
│   └── optimizer_demo.c      # DP optimizer: cost 3-table join shapes
├── demos/
│   ├── mini-query-engine/
│   │   └── README.md         # Query engine walkthrough (250+ lines)
│   └── mini-join-algorithms/
│       └── README.md         # Join algorithms deep dive (250+ lines)
├── docs/
│   ├── course-alignment.md   # CMU 15-445 & PostgreSQL mapping
│   └── relational-db-internals.md  # Internals reference
├── Makefile
└── README.md
```

## Components

### SQL Parser
Hand-written recursive descent parser. Supports SELECT (with WHERE, ORDER BY), INSERT, CREATE TABLE, DROP TABLE. Handles INT, VARCHAR(n), TEXT types. Comparison operators: =, !=, <, <=, >, >=.

### Query Optimizer
Cost-based optimizer with 9 plan node types. Uses dynamic programming for bottom-up join enumeration on 2-3 table joins. Cost model accounts for sequential I/O, CPU comparison, sort, and join costs.

### Volcano Executor
Implements the Volcano iterator model (open/next/close). Executors: SeqScan, Filter, Project, Sort, HashJoin, NestedLoopJoin, SortMergeJoin, Agg.

### Join Algorithms
Four join implementations with comparative benchmarking:
- **Hash Join**: O(outer+inner), build hash table from inner, probe with outer
- **Nested-Loop Join**: O(outer*inner), double loop
- **Sort-Merge Join**: O(N log N), sort both sides, merge
- **GRACE Hash Join**: Partitioned hash for large datasets

### Row Store
Row-based (NSM) storage with fixed-size row array. Supports table creation, row insertion, sequential scan, sorting, and deletion marking.

## Building

```bash
make              # Build static library: build/librelationaldb.a
make examples     # Build demos: build/sql_demo, build/join_demo, build/optimizer_demo
make all          # Build everything
make clean        # Remove build artifacts
make run-sql      # Build and run sql_demo
make run-join     # Build and run join_demo
make run-optimizer# Build and run optimizer_demo
```

## Requirements

- C99 compiler (gcc, clang)
- libc + libm (no external dependencies)
- Windows: MinGW or MSVC-compatible environment

## Demos

| Command              | Demo                               |
|---------------------|------------------------------------|
| `make run-sql`      | Parse SQL, build table, execute volcano pipeline |
| `make run-join`     | Compare all 4 join algorithms on users+orders |
| `make run-optimizer`| DP optimizer: enumerate join orders for 3 tables |
