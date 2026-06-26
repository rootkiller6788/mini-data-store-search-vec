#ifndef MINI_SSTABLE_H
#define MINI_SSTABLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define SSTABLE_MAGIC  0x88E2416B
#define BLOCK_SIZE     4096
#define MAX_KEY_SIZE   256
#define MAX_VALUE_SIZE 1024
#define RESTART_INTERVAL 16

/* ─────────────────────────────────────────────
   SSTable Data Block
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t num_entries;
    uint32_t data_size;
    uint8_t  *data;          /* serialized key-value pairs  */
    uint32_t num_restarts;
    uint32_t *restart_offsets; /* offsets of restart points */
} SSTableDataBlock;

/* ─────────────────────────────────────────────
   SSTable Index Block
   ───────────────────────────────────────────── */
typedef struct {
    uint8_t   last_key[MAX_KEY_SIZE];
    uint32_t  last_key_len;
    uint32_t  block_offset;
    uint32_t  block_size;
} SSTableIndexEntry;

typedef struct {
    uint32_t          num_entries;
    SSTableIndexEntry *entries;
} SSTableIndexBlock;

/* ─────────────────────────────────────────────
   SSTable Footer  (fixed size: 48 bytes)
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t index_block_offset;
    uint32_t index_block_size;
    uint32_t magic_number;
    uint8_t  padding[36];  /* pad to 48 bytes */
} SSTableFooter;

/* ─────────────────────────────────────────────
   Bloom Filter
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t  seed1;
    uint32_t  seed2;
    uint32_t  seed3;
    uint32_t  bits_per_key;
    uint32_t  num_bits;
    uint32_t  num_bytes;
    uint8_t   *bit_array;
} BloomFilter;

/* ─────────────────────────────────────────────
   SSTable handle (in-memory representation)
   ───────────────────────────────────────────── */
typedef struct SSTable {
    FILE      *file;
    char      *filename;
    uint64_t  file_size;
    uint32_t  num_data_blocks;
    SSTableDataBlock  **data_blocks;
    SSTableIndexBlock index_block;
    SSTableFooter     footer;
    BloomFilter       bloom;
} SSTable;

/* ─────────────────────────────────────────────
   Merge iterator – node in the heap
   ───────────────────────────────────────────── */
typedef struct {
    SSTable   *table;
    uint32_t   block_idx;
    uint32_t   entry_idx;   /* within current block */
    uint8_t    key[MAX_KEY_SIZE];
    uint32_t   key_len;
    uint8_t    value[MAX_VALUE_SIZE];
    uint32_t   value_len;
    int        exhausted;
} SSTableMergeNode;

typedef struct {
    SSTableMergeNode **nodes;
    uint32_t           num_nodes;
    uint32_t           heap_size;
} SSTableMergeIterator;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

int  sstable_write(const char *filename,
                   const uint8_t **keys, const uint32_t *key_lens,
                   const uint8_t **values, const uint32_t *value_lens,
                   uint32_t num_entries);

SSTable *sstable_read(const char *filename);

int  sstable_get(SSTable *table,
                 const uint8_t *key, uint32_t key_len,
                 uint8_t *value_out, uint32_t *value_len_out);

int  bloom_maybe_contain(BloomFilter *bf,
                         const uint8_t *key, uint32_t key_len);

SSTableMergeIterator *sstable_merge_iterator_create(SSTable **tables, uint32_t num);
int  sstable_merge_iterator_next(SSTableMergeIterator *iter,
                                 uint8_t *key_out, uint32_t *key_len_out,
                                 uint8_t *value_out, uint32_t *value_len_out);
void sstable_merge_iterator_destroy(SSTableMergeIterator *iter);

void sstable_destroy(SSTable *table);

#endif /* MINI_SSTABLE_H */
