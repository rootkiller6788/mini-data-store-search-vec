# Relational Database Internals

> 参考: CMU 15-445 Database Systems, PostgreSQL source code

## Architecture Overview

```
                        +---+
                        |SQL|
                        +-+-+
                          |
                          v
                    +-----+------+
                    |   Parser   |
                    +-----+------+
                          |
                     Parse Tree
                          |
                          v
                    +-----+------+
                    |  Optimizer |
                    +-----+------+
                          |
                     Plan Tree
                          |
                          v
                    +-----+------+
                    |  Executor  |
                    +-----+------+
                          |
                    Result Tuples
```

### 1. SQL Parser

Converts SQL text into an Abstract Syntax Tree (AST). Our hand-written parser handles:
- SELECT with column list, FROM clause, optional WHERE and ORDER BY
- CREATE TABLE with typed column definitions
- INSERT with VALUES
- DROP TABLE

**Tokenization**: Simple whitespace/identifier/value tokenizer that handles keywords case-insensitively, quoted strings, numeric literals, and comparison operators.

**Grammar rules** (simplified):
```
SELECT     := 'SELECT' cols 'FROM' table ['WHERE' cond] ['ORDER BY' col]
CREATE     := 'CREATE' 'TABLE' table '(' col_def (',' col_def)* ')'
INSERT     := 'INSERT' 'INTO' table 'VALUES' '(' val (',' val)* ')'
DROP       := 'DROP' 'TABLE' table
cond       := col op val
op         := '=' | '!=' | '<' | '<=' | '>' | '>='
col_def    := name type ['(' length ')']
type       := 'INT' | 'VARCHAR' | 'TEXT'
```

### 2. Query Optimizer

Transforms a logical query plan into an efficient physical plan using cost estimation.

**Cost-Based Optimization:**

The optimizer estimates:
- **Startup cost**: Cost to produce the first tuple
- **Total cost**: Cost to produce all tuples
- **Cardinality** (rows): Estimated number of output rows
- **Width**: Average tuple width in bytes

**Cost Formulas:**

| Operator         | Startup            | Per-Tuple          | Total Formula |
|-----------------|--------------------|--------------------|---------------|
| SeqScan         | 0                  | cpu_tuple_cost     | pages * seq_page_cost |
| Filter          | child.startup      | cpu_operator_cost  | child + child.rows * 0.01 |
| Projection      | child.startup      | cpu_tuple_cost     | child + child.rows * 0.01 |
| Sort            | child.total        | comp cost          | child + N log N * cpu_operator_cost |
| HashJoin        | build + probe      | 0                  | (outer + inner) * 0.5 |
| NestedLoop      | 0                  | inner cost         | outer * inner * 0.05 |
| SortMerge       | sort both          | merge cost         | sort(outer) + sort(inner) + scan |

**Selectivity Estimation:**

Simple heuristic-based selectivity:
- Equality (=): 0.01 (1% selectivity — assumes 100 distinct values)
- Not Equal (!=): 0.99
- Range (<, <=, >, >=): 0.33 (assumes uniform distribution over 3 intervals)

**Dynamic Programming Join Enumeration:**

For N-way joins, enumerate all join orderings bottom-up:
1. Start with base plans (SeqScan on each table)
2. For each pair of disjoint subsets, consider joining
3. For each join shape, try HashJoin, NestedLoop, SortMerge
4. Keep the plan with lowest estimated total cost
5. Continue until all tables are joined

This mirrors System R's classic DP optimizer (Selinger et al., 1979), simplified for clarity.

### 3. Volcano Executor

The Volcano iterator model (Graefe, 1994) is a pull-based execution model. Each operator implements:

```c
void open()  — initialize, allocate resources, open child operators
Tuple next() — return next output tuple (pull from children as needed)
void close() — release resources, close children
```

**Data Flow**: Tuples are pulled upward through the tree. The root operator calls `next()` on its children, which may recursively pull from their children. Control flows top-down; data flows bottom-up.

**Momentum/Cancellation**: When a user stops reading results early, the remaining `next()` calls are never made, and `close()` is called to free resources.

### 4. Join Algorithms

#### Hash Join (1-Pass)

Assumes inner relation fits in memory.

```
BUILD phase:
  for each tuple t in inner:
    hash_table.insert(t)

PROBE phase:
  for each tuple r in outer:
    matches = hash_table.lookup(r.key)
    for each match m:
      emit(r, m)
```

Time: O(|outer| + |inner|), Memory: O(|inner|)

#### Nested-Loop Join

```
for each tuple r in outer:
  for each tuple s in inner:
    if r.key == s.key:
      emit(r, s)
```

Time: O(|outer| * |inner|). Only join algorithm that supports non-equality conditions (theta joins).

#### Sort-Merge Join

```
sort outer by join_key
sort inner by join_key

i = 0; j = 0
while i < |outer| and j < |inner|:
  if outer[i].key < inner[j].key: i++
  elif outer[i].key > inner[j].key: j++
  else:
    // keys equal — match and advance
    emit(outer[i], inner[j])
    j++  // handle duplicates in inner
```

Time: O(|outer| log |outer| + |inner| log |inner| + |outer| + |inner|)

#### GRACE Hash Join

When inner doesn't fit in memory, partition both relations:

```
PARTITION phase:
  Split outer into N buckets using hash(key) % N
  Split inner into N buckets using hash(key) % N

JOIN phase:
  for each bucket i:
    hash_join(bucket_outer[i], bucket_inner[i])
```

### 5. Row Storage (N-ary Storage Model)

Rows are stored contiguously in a `Table` struct with a fixed-size row array:

```c
typedef struct {
    char name[TABLE_MAX_NAME];
    Schema schema;               // column metadata
    int   num_rows;
    Row   rows[TABLE_MAX_ROWS];  // fixed array of tuples
} Table;
```

Each `Row` contains N field values as char arrays plus a deletion flag:

```c
typedef struct {
    char values[ROW_MAX_COLUMNS][ROW_MAX_VALUE];
    int  num_fields;
    int  is_deleted;  // logical deletion mark
} Row;
```

This is a simplified N-ary Storage Model (NSM) — all columns of a row are stored together, which is optimal for transactional (OLTP) workloads.

### 6. Schema Management

**Column Types**: INT (32-bit integer), VARCHAR(n) (variable-length string), TEXT (unlimited string).

**Catalog**: Stores table names and statistics (number of rows, number of pages) used by the optimizer for cost estimation.

## PostgreSQL Reference Architecture

| Component        | PostgreSQL Location                | File |
|-----------------|-----------------------------------|------|
| SQL Parser      | `src/backend/parser/`             | `gram.y`, `scan.l` |
| Optimizer       | `src/backend/optimizer/`          | `path/`, `plan/` |
| Executor        | `src/backend/executor/`           | `node*.c` |
| Storage (Heap)  | `src/backend/access/heap/`        | `heapam.c` |
| Hash Join       | `src/backend/executor/`           | `nodeHashjoin.c` |
| Nested Loop     | `src/backend/executor/`           | `nodeNestloop.c` |
| Merge Join      | `src/backend/executor/`           | `nodeMergejoin.c` |

## Further Reading

- Graefe, G. (1993). "Query Evaluation Techniques for Large Databases". *ACM Computing Surveys*.
- Graefe, G. (1994). "Volcano — An Extensible and Parallel Query Evaluation System". *IEEE TKDE*.
- Selinger, P. G., et al. (1979). "Access Path Selection in a Relational Database Management System". *SIGMOD*.
- Garcia-Molina, H., Ullman, J. D., Widom, J. (2009). "Database Systems: The Complete Book".
- PostgreSQL Documentation: Chapters 50-54 (Internals).
