#ifndef MINI_RANGE_QUERY_H
#define MINI_RANGE_QUERY_H

#include <stdint.h>
#include <stddef.h>
#include "sstable.h"

/* ─────────────────────────────────────────────
   Range Query — ordered key range scan over SSTables

   Algorithm: Multi-source ordered traversal using a min-heap
   merge iterator across all SSTables in a level, combined with
   key range filtering.

   Theorem (Range Query Complexity):
     For an LSM tree with B levels, a range query covering R rows
     scans O(R + B·K) entries where K is the number of SSTables
     overlapping the key range, using an O(K)-size min-heap.

   Reference: P. O'Neil et al., "The Log-Structured Merge-Tree",
   Acta Informatica, 1996.
   ───────────────────────────────────────────── */

#define RANGE_MAX_KEY_LEN   256
#define RANGE_MAX_VALUE_LEN 1024
#define RANGE_BATCH_SIZE    64

/* ─────────────────────────────────────────────
   Key range specification
   ───────────────────────────────────────────── */
typedef struct {
    uint8_t  start_key[RANGE_MAX_KEY_LEN];
    uint32_t start_key_len;
    int      start_inclusive;       /* 1 = include start key */
    uint8_t  end_key[RANGE_MAX_KEY_LEN];
    uint32_t end_key_len;
    int      end_inclusive;         /* 1 = include end key */
} KeyRange;

/* ─────────────────────────────────────────────
   Range scan result entry
   ───────────────────────────────────────────── */
typedef struct {
    uint8_t  key[RANGE_MAX_KEY_LEN];
    uint32_t key_len;
    uint8_t  value[RANGE_MAX_VALUE_LEN];
    uint32_t value_len;
} RangeEntry;

/* ─────────────────────────────────────────────
   Range iterator — streaming scan results
   ───────────────────────────────────────────── */
typedef struct RangeIterator RangeIterator;

/* ─────────────────────────────────────────────
   Range query filter callback
   Return 1 to include entry, 0 to skip.
   ───────────────────────────────────────────── */
typedef int (*RangeFilterFn)(const uint8_t *key, uint32_t key_len,
                             const uint8_t *value, uint32_t value_len,
                             void *ctx);

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

/* Single SSTable range scan — collects all entries in [start, end) */
int  sstable_range_scan(SSTable *table, const KeyRange *range,
                         RangeEntry *results, uint32_t max_results,
                         uint32_t *num_results);

/* Multi-SSTable range scan with merge deduplication */
int  sstable_multi_range_scan(SSTable **tables, uint32_t num_tables,
                               const KeyRange *range,
                               RangeEntry *results, uint32_t max_results,
                               uint32_t *num_results);

/* Create a streaming range iterator from a single SSTable */
RangeIterator *range_iterator_create(SSTable *table, const KeyRange *range);

/* Get next entry from range iterator (returns 1=has next, 0=done) */
int  range_iterator_next(RangeIterator *iter,
                         RangeEntry *entry_out);

/* Apply filter to range scan results in-place */
int  range_filter(RangeEntry *entries, uint32_t num_entries,
                  RangeFilterFn filter, void *ctx,
                  RangeEntry *filtered, uint32_t max_filtered,
                  uint32_t *num_filtered);

/* Compare two keys for ordering (returns <0, 0, >0) */
int  key_compare(const uint8_t *a, uint32_t a_len,
                 const uint8_t *b, uint32_t b_len);

/* Check if a key falls within a range */
int  key_in_range(const uint8_t *key, uint32_t key_len,
                  const KeyRange *range);

/* Destroy range iterator */
void range_iterator_destroy(RangeIterator *iter);

#endif /* MINI_RANGE_QUERY_H */
