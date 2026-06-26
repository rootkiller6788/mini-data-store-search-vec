# Course Alignment — CMU 15-445 & PostgreSQL Internals

This section maps `mini-relational-db` implementation components to corresponding topics in CMU 15-445/645 (Database Systems) and PostgreSQL internals.

## Mapping Table

| mini-relational-db                   | CMU 15-445 Topic                        | PostgreSQL Equivalent |
|--------------------------------------|----------------------------------------|-----------------------|
| `include/sql_parser.h` + `src/sql_parser.c` | SQL parsing (Lex & YACC)               | `src/backend/parser/` — gram.y, scan.l, parse_* functions |
| `include/query_optimizer.h` + `src/query_optimizer.c` | Query Optimization (Lectures 13-14)    | `src/backend/optimizer/` — plan nodes, cost estimation, join enumeration |
| `include/volcano_executor.h` + `src/volcano_executor.c` | Query Execution (Lectures 11-12)       | `src/backend/executor/` — ExecSeqScan, ExecFilter, ExecSort, ExecHashJoin |
| `include/join_algorithms.h` + `src/join_algorithms.c` | Join Algorithms (Lecture 12)           | `src/backend/executor/nodeHashjoin.c`, `nodeNestloop.c`, `nodeMergejoin.c` |
| `include/row_store.h` + `src/row_store.c` | Storage Layer (Lectures 4-6)           | `src/backend/access/heap/` — heapam.c (row-based heap storage) |
| `examples/sql_demo.c`                | End-to-end query evaluation            | `psql` client + `exec_simple_query()` in `postgres.c` |
| `examples/join_demo.c`               | Join algorithm benchmarking            | `contrib/` extensions for join testing |
| `examples/optimizer_demo.c`          | DP optimizer, cost model               | `make_join_rel()`, `add_paths_to_joinrel()` in `path/joinpath.c` |

## CMU 15-445 Lecture Alignment

### Lecture 04-06: Database Storage (row_store.c)

- **Page layout**: Each `Table` holds up to `TABLE_MAX_ROWS` (1024) fixed-width rows
- **Tuple format**: `Row` struct — values[N] as char arrays, `is_deleted` flag for MVCC-like deletion
- **N-ary Storage Model (NSM)**: All fields of a row stored contiguously (row-based)

### Lecture 09-10: Indexes & B+Trees

The `table_create_index` placeholder is designed to integrate with the kernel-level B+tree from the mini-hardware module. A B+tree index maps key values to row IDs (rids), enabling point and range queries.

### Lecture 11: Query Execution I (volcano_executor.c)

- **Volcano Iterator Model**: Every operator implements `open()`, `next()`, `close()`
- **Pull-based execution**: Data flows bottom-up; `next()` on root pulls tuples through the tree
- **Executor types**: SeqScan, Filter, Projection, Sort, Aggregation

### Lecture 12: Join Algorithms (join_algorithms.c)

- **Hash Join**: Build phase (inner -> hash table), Probe phase (outer -> lookup)
- **Nested-Loop Join**: Double for-loop; can benefit from index on inner
- **Sort-Merge Join**: Sort both sides by join key; merge with two pointers
- **GRACE Hash Join**: Recursive partitioning when inner exceeds memory

### Lecture 13-14: Query Optimization (query_optimizer.c)

- **Cost Model**: Estimates based on page I/O (seq page cost = 0.1), CPU (comparison cost), and cardinality
- **Selectivity Estimation**: Equality = 1/NDV, Range = 1/3, using simple heuristics
- **Dynamic Programming**: Bottom-up enumeration of join orders for up to 3 tables
- **Plan Nodes**: Physical operator types (SeqScan, HashJoin, NestedLoop, Sort, etc.)

## PostgreSQL Internals References

### Parser (`src/backend/parser/`)

PostgreSQL uses `flex` (scan.l) and `bison` (gram.y) for lexing and parsing. Our hand-written parser mirrors this with a simpler recursive descent approach suitable for educational purposes.

Key PostgreSQL source files:
- `src/backend/parser/parser.c` — `raw_parser()`
- `src/backend/parser/gram.y` — SQL grammar rules
- `src/backend/parser/scan.l` — lexer rules

### Executor (`src/backend/executor/`)

PostgreSQL's executor uses the same Volcano model. Each plan node type has `ExecInitNode()`, `ExecProcNode()`, and `ExecEndNode()`, analogous to our `open()`, `next()`, `close()`.

Key PostgreSQL source files:
- `src/backend/executor/execMain.c` — `ExecutorRun()`
- `src/backend/executor/nodeSeqscan.c` — sequential scan
- `src/backend/executor/nodeHashjoin.c` — hash join (Hybrid Hash Join optimization)
- `src/include/nodes/execnodes.h` — executor state structures

### Optimizer (`src/backend/optimizer/`)

PostgreSQL uses a bottom-up dynamic programming optimizer with genetic algorithm fallback for many-table joins.

Key PostgreSQL source files:
- `src/backend/optimizer/path/allpaths.c` — `make_one_rel()`
- `src/backend/optimizer/path/joinpath.c` — join path generation
- `src/backend/optimizer/path/costsize.c` — `cost_seqscan()`, `cost_nestloop()`, `cost_hashjoin()`
- `src/backend/optimizer/plan/planner.c` — top-level planner entry point

### Storage (`src/backend/access/heap/`)

PostgreSQL's heap storage is page-based with tuple-level MVCC (xmin/xmax). Our simplified row store skips page layout and MVCC for clarity.

Key PostgreSQL source files:
- `src/backend/access/heap/heapam.c` — heap access methods
- `src/include/access/htup_details.h` — `HeapTupleHeaderData` (tuple format)
- `src/include/storage/bufpage.h` — page layout (100+ line header)

## Summary

`mini-relational-db` implements a simplified but architecturally faithful subset of a relational database engine:
- SQL parser (hand-written, no lex/yacc dependency)
- Rule-based + cost-based query optimizer
- Volcano iterator model executor
- Row-based storage layer
- Four join algorithms with comparative benchmarking

Each component maps directly to a major subsystem in PostgreSQL, making this an effective teaching tool for understanding database internals before tackling production-grade codebases.
