# Course Alignment — mini-file-storage

This document maps `mini-file-storage` components to common CS curriculum topics:

## Data Structures & Algorithms

| Topic | Component | Implementation |
|-------|-----------|---------------|
| Skip List | `skiplist.h/c` | Leveled linked list with O(log n) search/insert, p=0.5 promotion |
| Multi-way Merge (K-way) | `compaction.c` | Min-heap on SSTable iterators, O(N log K) |
| Binary Search | `sstable.c` | Index block search for right data block |
| Hash Functions | `sstable.c` (Bloom) | Murmur3-like hash with 3 seeds |
| Prefix Compression / Delta Encoding | `sstable.c` | Shared prefix encoding in data blocks |
| Min-Heap | `sstable.c` (merge iterator) | Heap operations for K-way merge |
| Probabilistic Data Structures | `sstable.c` (Bloom Filter) | k=3, bits_per_key=10, FPR ~0.82% |

## Database Systems

| Topic | Component | Implementation |
|-------|-----------|---------------|
| LSM-Tree | `lsm_tree.h/c` | Memtable → Immutable → SSTable levels |
| Write-Ahead Logging | `wal_file.h/c` | CRC32C checksums, PUT/DELETE records, crash recovery |
| Compaction | `compaction.h/c` | Leveled & Tiered/Universal strategies |
| Read/Write Amplification | `demos/mini-compaction-strategies/` | Theoretical analysis and trade-offs |
| Bloom Filter Integration | `sstable.c` | Per-SSTable filter, reject absent keys |
| Crash Recovery | `wal_file.c` + `lsm_tree.c` | WAL replay on open, checksum validation |
| File Format Design | `sstable.h/c` | Block layout, footer, index, encoding |

## Operating Systems

| Topic | Component | Implementation |
|-------|-----------|---------------|
| File I/O | All `src/*.c` | `fopen`, `fread`, `fwrite`, `fseek`, `ftell` |
| Buffered vs Direct I/O | `wal_file.c` | `fflush` for WAL sync |
| Memory Management | All | `malloc`, `realloc`, `calloc`, `free` |
| File System Semantics | `lsm_tree.c` | `mkdir`, `remove` for SSTable lifecycle |

## Software Engineering

| Topic | Component | Implementation |
|-------|-----------|---------------|
| Modular Design | `include/` + `src/` | Separation of concerns, clear APIs |
| Header-only vs Implementation | `include/*.h` + `src/*.c` | C99 linkage, include guards |
| Error Handling | All | Return codes (-1/0/1), NULL checks |
| Memory Discipline | All | `calloc` + `free` pairing, `valgrind`-checkable |
| Build System | `Makefile` | Target-based build, examples, clean |

## Distributed Systems (Foundation)

| Topic | Relevance |
|-------|-----------|
| LSM-Tree | Foundation of Cassandra, ScyllaDB, HBase, BigTable |
| SSTable | Used in ScyllaDB, Cassandra, LevelDB/RocksDB |
| Compaction Strategies | Size-Tiered (Cassandra), Leveled (HBase), Universal (RocksDB) |
| WAL | Foundation of all durable distributed stores |

## Learning Objectives

After studying `mini-file-storage`, students should be able to:

1. **Implement** a Skip List with probabilistic level generation
2. **Design** an on-disk file format with index and footer
3. **Explain** how Bloom Filters reduce unnecessary I/O
4. **Compare** Leveled vs Tiered Compaction strategies
5. **Trace** the read/write path through an LSM-Tree
6. **Implement** WAL crash recovery
7. **Analyze** write/read/space amplification trade-offs
8. **Write** data with prefix compression to save space
9. **Understand** how production databases (LevelDB, RocksDB, ScyllaDB) work
