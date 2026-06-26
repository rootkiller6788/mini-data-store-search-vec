# mini-join-algorithms — Join Algorithms: Hash, Nested-Loop, Sort-Merge, GRACE Hash

> 参考 CMU 15-445 Lecture 12 — Hash Join, Nested-Loop Join, Sort-Merge Join, GRACE Hash Join

## Overview

Implements four classic join algorithms and compares their efficiency on the same dataset.

| Algorithm    | Complexity | Best for |
|-------------|------------|----------|
| Hash Join   | O(outer + inner) | Equality joins, inner fits in memory |
| Nested Loop | O(outer * inner) | Small tables, cross joins, theta joins |
| Sort-Merge  | O(N log N) | Both sides sorted or sortable, equi-joins |
| GRACE Hash  | O(outer + inner) | Partitioned hash; large inner |

### Hash Join (1-Pass)

```
Phase 1 (BUILD):  Scan inner, hash each row by join key into bucket[hash(key)]
Phase 2 (PROBE):  Scan outer, hash each outer key, probe bucket, emit matches
```

If inner table fits in memory, this is the most efficient join algorithm for equality conditions.

### Nested-Loop Join (NLJ)

```
for each row r in outer:
    for each row s in inner:
        if r.key == s.key:
            emit (r, s)
```

Simple but `O(|outer| * |inner|)` comparisons. Can use an index on the inner to reduce cost (index nested-loop join).

### Sort-Merge Join

```
Step 1: Sort outer on join key
Step 2: Sort inner on join key
Step 3: Two-pointer merge
        while i < |outer| and j < |inner|:
            if outer[i].key < inner[j].key: i++
            elif outer[i].key > inner[j].key: j++
            else: emit match, j++
```

### GRACE Hash Join

When the inner table is too large to fit in memory, GRACE hash join partitions both tables into N buckets using the same hash function. Each partition is then joined independently using 1-pass hash join.

```
Phase 1 (PARTITION): Hash both outer and inner into N disk-resident partitions
Phase 2 (JOIN):      For each partition pair (i,i), 1-pass hash join
Phase 3 (OUTPUT):    Collect all joined tuples
```

## Example: `examples/join_demo.c`

Two tables representing an e-commerce schema:

**users** (5 rows):
| uid | name  | email             |
|-----|-------|-------------------|
| 1   | Alice | alice@example.com |
| 2   | Bob   | bob@example.com   |
| 3   | Carol | carol@example.com |
| 4   | Dave  | dave@example.com  |
| 5   | Eve   | eve@example.com   |

**orders** (7 rows):
| oid | uid | amount |
|-----|-----|--------|
| 101 | 1   | 99     |
| 102 | 2   | 150    |
| 103 | 1   | 200    |
| 104 | 3   | 75     |
| 105 | 2   | 300    |
| 106 | 5   | 50     |
| 107 | 1   | 125    |

Join: `users JOIN orders ON users.uid = orders.uid`

**Output:**

```
=== Mini Relational DB - Join Algorithms Demo ===

Table 'users':
=== users (3 columns, 5 rows) ===
uid         | name        | email
...

Table 'orders':
=== orders (3 columns, 7 rows) ===
oid         | uid         | amount
...

=== Join: users JOIN orders ON uid = uid ===

[Hash Join]
  Hash Join: outer=5 inner=7 compare=5 hash=12 result=6
  compares=5  hash_ops=12  io_pages=1.05

[Nested Loop]
  Nested Loop Join: outer=5 inner=7 compares=35 result=6
  compares=35  hash_ops=0  io_pages=0.35

[Sort-Merge]
  Sort-Merge Join: outer=5 inner=7 compares=10 result=6
  compares=10  hash_ops=0  io_pages=7.05

[GRACE Hash]
  GRACE Hash Join: same as hash, partitions=4
  compares=5  hash_ops=12  io_pages=1.05

=== Efficiency Summary ===
Method           Compares  IO_Pages
-----------------------------------
Hash              5        1.05
Nested-Loop      35        0.35
Sort-Merge       10        7.05
```

**Analysis:**

- **Hash Join** has the fewest compares (5 = |outer|) since each outer row probes O(1)
- **Nested-Loop** does 35 compares (5 * 7) but minimal I/O since both tables are in memory
- **Sort-Merge** does 10 compares but adds sort overhead (~2*N log N I/O)
- **GRACE Hash** shows same stats as hash since tables fit in memory (no partitioning needed)

## Implementation Detail: Hash Table

The `HashTable` struct (in `include/join_algorithms.h`) uses a chain-based bucket array:

```c
typedef struct {
    JoinBucket buckets[JOIN_HT_SIZE];  // 256 buckets
    int        key_col;
} HashTable;
```

Each bucket holds a linked list of `JoinTuple` nodes. The hash function is DJB2:
```c
unsigned int h = 5381;
while ((c = *str++))
    h = ((h << 5) + h) + c;
return h % JOIN_HT_SIZE;
```

## Building

```bash
make && make examples
./build/join_demo
```

## References

- Blanas, S., et al. (2011). "Design and Evaluation of Main Memory Hash Join Algorithms for Multi-core CPUs". *SIGMOD*.
- Shapiro, L. D. (1986). "Join Processing in Database Systems with Large Main Memories". *TODS*.
- Kitsuregawa, M., et al. (1983). "Application of Hash to Data Base Machine and Its Architecture". *New Generation Computing*.
- CMU 15-445 Lecture 12: Join Algorithms.
