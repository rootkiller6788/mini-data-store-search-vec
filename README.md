# Mini Data Store Search Vec

A collection of **from-scratch, zero-dependency C implementations** of database kernels, storage engines, search systems, and vector databases. Each module models real data infrastructure behavior — from B+tree buffer pools to SQL query planners, LSM-tree compaction, inverted indices, and ANN vector search. Modules map to CMU 15-445/645, MIT 6.830, Stanford CS245 courses.

## Modules

| Module | Topics | Key References |
|--------|--------|----------------|
| [mini-db-kernel](mini-db-kernel/) | Buffer pool, B+tree index, WAL (redo/undo), ARIES recovery, lock manager (2PL), MVCC basics | CMU 15-445, Database Internals |
| [mini-relational-db](mini-relational-db/) | SQL parser (mini), query optimizer (cost-based), volcano iterator executor, JOIN algorithms (hash/nested-loop) | CMU 15-445, PostgreSQL |
| [mini-nosql-store](mini-nosql-store/) | Key-value store, LSM engine, document store (Mongo-like), column-family store (Bigtable model), CRUD API | DynamoDB, Bigtable, MongoDB |
| [mini-graph-db](mini-graph-db/) | Property graph model, adjacency list, BFS/DFS traversal, shortest path, PageRank, label propagation | Neo4j internals, Gremlin |
| [mini-message-stream](mini-message-stream/) | Topic/partition model, producer/consumer, consumer group rebalance, offset commit log, time-based retention | Kafka internals, Pulsar |
| [mini-search-engine](mini-search-engine/) | Inverted index, tokenizer/analyzer, TF-IDF scoring, BM25 relevance, phrase queries, boolean queries | Lucene internals, Elasticsearch |
| [mini-vector-db](mini-vector-db/) | Vector embedding, exact KNN, approximate ANN (LSH, IVF, HNSW, PQ), cosine/Euclidean distance | Milvus, FAISS, Annoy |
| [mini-file-storage](mini-file-storage/) | LSM-Tree engine, SSTable format (block index, bloom filter), leveled/universal compaction, WAL | LevelDB, RocksDB, ScyllaDB |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Storage engine simulation** — educational models of database internals
- **Theory-to-code mapping** — every module includes `docs/` with course-alignment notes
- **Practical demos** — B+tree visualizer, LSM compaction simulator, ANN index builder, and more

## Building

```bash
cd mini-db-kernel
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-data-store-search-vec/
├── mini-db-kernel/             # Database Kernel
├── mini-relational-db/         # Relational Database Engine
├── mini-nosql-store/           # NoSQL Stores
├── mini-graph-db/              # Graph Databases
├── mini-message-stream/        # Message Stream Processing
├── mini-search-engine/         # Full-Text Search
├── mini-vector-db/             # Vector Similarity Search
└── mini-file-storage/          # File Storage (LSM/SSTable)
```

## License

MIT
