# Storage Engine Architecture — mini-file-storage

## 整体架构

```
                      ┌─────────────┐
                      │   客户端 API │
                      │ lsm_put/get │
                      └──────┬──────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        ┌──────────┐  ┌───────────┐  ┌──────────┐
        │   WAL    │  │ Memtable  │  │ Manifest │
        │ (wal_file)│  │(skiplist) │  │  (TODO)  │
        └──────────┘  └─────┬─────┘  └──────────┘
                             │
                      Freeze & Flush
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        ┌───────────┐ ┌───────────┐ ┌───────────┐
        │  Level 0  │ │  Level 1  │ │  ... L6   │
        │ SSTables  │ │ SSTables  │ │ SSTables  │
        └─────┬─────┘ └─────┬─────┘ └─────┬─────┘
              │              │              │
              └──────────────┼──────────────┘
                             │
                        Compaction
                             │
                    ┌────────┴────────┐
                    ▼                 ▼
              Leveled Comp.    Tiered Comp.
```

## 数据流

### Write Path
```
User put(key, value)
  → WAL append (sync to disk)
  → SkipList insert (memtable)
  → When memtable full:
      Freeze → Immutable queue
      Background flush → SSTable (Level 0)
  → Compaction scheduler:
      Level 0 → Level 1 → ... → Level 6
```

### Read Path
```
User get(key)
  → Memtable search (SkipList)
  → Immutable queue search (newest first)
  → Level 0 SST: check all files (newest first)
      → Bloom filter check
      → Index binary search
      → Block search
  → Level 1...6 SST: binary search files
      → Bloom filter check
      → Data block search
  → Return value or NOT_FOUND
```

## Component Details

| Component | File | Purpose |
|-----------|------|---------|
| SkipList | skiplist.h/c | In-memory sorted structure, Memtable |
| SSTable | sstable.h/c | On-disk sorted immutable file |
| WAL | wal_file.h/c | Write-ahead log for crash recovery |
| Compaction | compaction.h/c | Multi-way merge, Leveled/Tiered |
| LSM Tree | lsm_tree.h/c | Orchestrator, coordinates all components |

## File Format (SSTable)

```
[SSTable File]
├── Data Block 0  (4KB)
│   ├── Header: num_entries, data_size, num_restarts
│   ├── Entries (prefix-compressed)
│   └── Restart offsets array
├── Data Block 1
├── ...
├── Data Block N
├── Index Block
│   ├── num_entries
│   └── [last_key, block_offset, block_size] × N
├── Bloom Filter (configurable bits_per_key)
└── Footer (48 bytes)
    ├── index_block_offset (4B)
    ├── index_block_size (4B)
    └── magic_number (4B, 0x88E2416B)
```

## Data Encoding (per entry)

| Field | Size | Description |
|-------|------|-------------|
| shared_len | 4B | Bytes shared with previous key |
| non_shared_len | 4B | Unique bytes of this key |
| value_len | 4B | Length of value |
| key_delta | non_shared_len B | Unique key suffix |
| value | value_len B | Value bytes |

## WAL Record Format

| Field | Size | Description |
|-------|------|-------------|
| checksum | 4B | CRC32C over payload |
| length | 4B | Payload length |
| type | 1B | PUT (0x01) or DELETE (0x02) |
| key_len | 4B | Key length |
| value_len | 4B | Value length |
| key | key_len B | Key data |
| value | value_len B | Value data |

## Constant Definitions

| Constant | Value | Meaning |
|----------|-------|---------|
| SSTABLE_MAGIC | 0x88E2416B | File magic number |
| BLOCK_SIZE | 4096 | Data block target size |
| MAX_KEY_SIZE | 256 | Maximum key size |
| MAX_VALUE_SIZE | 1024 | Maximum value size |
| RESTART_INTERVAL | 16 | Entries per restart point |
| SKIPLIST_MAX_LEVEL | 12 | Max skip list height |
| LSM_MAX_LEVELS | 7 | LSM tree levels (L0-L6) |
| LSM_MEMTABLE_SIZE | 64KB | Memtable flush threshold |
| WAL_MAGIC | 0xC0A1B0C4 | WAL magic number |
| WAL_BLOCK_SIZE | 32KB | WAL write granularity |

## Compaction Strategies

### Leveled Compaction
- Level 0: ≤4 files, allow overlaps
- Level N (N≥1): sorted, non-overlapping
- Level N+1 is 10× larger than Level N
- Write amplification: ~10-30×
- Read amplification: ~7 files max

### Tiered (Universal) Compaction
- Files grouped by similar size
- When group has ≥K files, merge into one
- Write amplification: ~4-10×
- Read amplification: ≤K files per tier

## Implementation Notes

- C99 standard, libc+libm only
- Single-threaded (no concurrency control)
- Fixed key/value size limits
- No compression (Snappy/Zstd)
- No block cache (reads always from disk/OS page cache)
- Educational focus: clear code over performance

## Next Steps / Possible Extensions

1. Add Manifest file for SSTable metadata tracking
2. Implement Snappy/LZ4 block compression
3. Add Block Cache (LRU) for hot data
4. Implement Checksums per block (CRC32C)
5. Support variable-length key/value (remove fixed limits)
6. Add Iterator for range scans across levels
7. Implement WriteBatch for atomic multi-put
8. Support multiple Column Families
9. Add Compaction Filter hooks
10. Implement WAL rotation and recycling
