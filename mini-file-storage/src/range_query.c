/* ───────────────────────────────────────────────────────────
   Range Query — ordered key range scan over SSTable(s).

   Implements:
     1. Single SSTable range scan with binary search start point
     2. Multi-SSTable merge scan with deduplication
     3. Streaming range iterator for memory-efficient traversal
     4. Key filtering with user-defined predicates

   Key insight: Range queries on LSM trees require merging results
   from memtable, all immutables, and all SSTable levels. For SSTables,
   the data is already sorted within each file, so we only need to
   merge the overlapping files.

   Reference: Luo et al., "LSM-based Storage Techniques: A Survey",
   The VLDB Journal, 2020.
   ─────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "range_query.h"

/* ─────────────────────────────────────────────
   Key comparison (lexicographic)
   ───────────────────────────────────────────── */
int key_compare(const uint8_t *a, uint32_t a_len,
                const uint8_t *b, uint32_t b_len) {
    uint32_t min_len = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, min_len);
    if (cmp != 0) return cmp;
    if (a_len < b_len) return -1;
    if (a_len > b_len) return 1;
    return 0;
}

/* ─────────────────────────────────────────────
   Check if key falls within a range [start, end)
   ───────────────────────────────────────────── */
int key_in_range(const uint8_t *key, uint32_t key_len,
                 const KeyRange *range) {
    if (!key || !range) return 0;

    /* Check lower bound */
    int cmp_start = key_compare(key, key_len,
                                range->start_key, range->start_key_len);
    if (range->start_inclusive) {
        if (cmp_start < 0) return 0;
    } else {
        if (cmp_start <= 0) return 0;
    }

    /* Check upper bound */
    int cmp_end = key_compare(key, key_len,
                              range->end_key, range->end_key_len);
    if (range->end_inclusive) {
        if (cmp_end > 0) return 0;
    } else {
        if (cmp_end >= 0) return 0;
    }

    return 1;
}

/* ─────────────────────────────────────────────
   Internal: find first data block that could contain
   keys >= range->start_key using binary search on index.
   ───────────────────────────────────────────── */
static int find_start_block(SSTable *table, const KeyRange *range) {
    if (!table->index_block.num_entries) return -1;

    uint32_t lo = 0, hi = table->index_block.num_entries;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        SSTableIndexEntry *e = &table->index_block.entries[mid];
        int cmp = key_compare(range->start_key, range->start_key_len,
                               e->last_key, e->last_key_len);
        if (cmp > 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= table->index_block.num_entries) return -1;
    return (int)lo;
}

/* ─────────────────────────────────────────────
   Decode a key-value entry from a data block at given
   restart point area, scanning linearly from start offset.
   Returns 1 if an entry was decoded, 0 if block exhausted.
   ───────────────────────────────────────────── */
static int decode_kv_from_block(SSTableDataBlock *db, uint32_t offset,
                                 uint8_t *key_out, uint32_t *key_len_out,
                                 uint8_t *value_out, uint32_t *value_len_out,
                                 uint32_t *next_offset) {
    if (offset >= db->data_size) return 0;

    uint32_t pos = offset;
    uint32_t shared    = *(uint32_t *)(db->data + pos); pos += 4;
    uint32_t nonshared = *(uint32_t *)(db->data + pos); pos += 4;
    uint32_t vlen      = *(uint32_t *)(db->data + pos); pos += 4;

    if (pos + nonshared + vlen > db->data_size) return 0;

    /* prev_key persists across sequential calls for delta encoding.
       For restart points (shared=0), the full key is in the entry.
       For delta entries (shared>0), reconstruct from prev_key + new bytes. */
    static uint8_t prev_key[RANGE_MAX_KEY_LEN];
    static uint32_t prev_expected_offset = 0xFFFFFFFF;

    /* Reset prev_key if we jumped to a non-consecutive position */
    if (offset != prev_expected_offset) {
        memset(prev_key, 0, RANGE_MAX_KEY_LEN);
    }

    if (shared == 0) {
        memcpy(key_out, db->data + pos, nonshared);
        *key_len_out = nonshared;
    } else {
        memcpy(key_out, prev_key, shared);
        memcpy(key_out + shared, db->data + pos, nonshared);
        *key_len_out = shared + nonshared;
    }
    pos += nonshared;

    if (vlen <= RANGE_MAX_VALUE_LEN) {
        memcpy(value_out, db->data + pos, vlen);
    } else {
        vlen = RANGE_MAX_VALUE_LEN;
        memcpy(value_out, db->data + pos, RANGE_MAX_VALUE_LEN);
    }
    *value_len_out = vlen;
    pos += vlen;

    /* Update prev_key and expected next offset */
    memcpy(prev_key, key_out, *key_len_out);
    prev_expected_offset = pos;  /* next decode should start here */

    *next_offset = pos;
    return 1;
}

/* ─────────────────────────────────────────────
   Single SSTable range scan — collect all entries
   in the given key range.
   ───────────────────────────────────────────── */
int sstable_range_scan(SSTable *table, const KeyRange *range,
                        RangeEntry *results, uint32_t max_results,
                        uint32_t *num_results) {
    if (!table || !range || !results || max_results == 0 || !num_results) return -1;

    *num_results = 0;

    /* Find starting block */
    int start_blk = find_start_block(table, range);
    if (start_blk < 0) return 0;

    /* We use a simpler approach: start from restart points */
    for (uint32_t bi = (uint32_t)start_blk;
         bi < table->num_data_blocks && *num_results < max_results; bi++) {

        SSTableDataBlock *db = table->data_blocks[bi];
        if (!db || db->num_restarts == 0) continue;

        /* Decode the first key of the block to get rough bounds */
        uint8_t first_key[256], last_key[256];
        uint32_t first_len = 0, last_len = 0, dummy;
        uint32_t next_off;
        /* Decode first entry at restart[0] */
        if (!decode_kv_from_block(db, db->restart_offsets[0],
                                   first_key, &first_len,
                                   last_key, &last_len, &next_off)) {
            continue;
        }
        /* Decode last entry near data_size */
        /* We approximate: if end_key < first_key, skip block */
        int cmp_end_vs_first = key_compare(range->end_key, range->end_key_len,
                                            first_key, first_len);
        if (!range->end_inclusive && cmp_end_vs_first <= 0) break;
        if (range->end_inclusive && cmp_end_vs_first < 0) break;

        /* Scan all entries in this block starting from appropriate restart */
        uint32_t r_start = 0;
        /* Find restart point >= start_key via binary search on restarts */
        {
            uint32_t lo_r = 0, hi_r = db->num_restarts;
            while (lo_r < hi_r) {
                uint32_t mid = lo_r + (hi_r - lo_r) / 2;
                uint32_t off = db->restart_offsets[mid];
                uint8_t rkey[256];
                uint32_t rkey_len;
                if (!decode_kv_from_block(db, off, rkey, &rkey_len,
                                           last_key, &dummy, &next_off)) {
                    lo_r = mid + 1;
                    continue;
                }
                int cmp = key_compare(range->start_key, range->start_key_len,
                                       rkey, rkey_len);
                if (cmp > 0) {
                    lo_r = mid + 1;
                } else {
                    hi_r = mid;
                }
            }
            r_start = (lo_r > 0) ? lo_r - 1 : 0;
        }

        uint32_t pos = db->restart_offsets[r_start];
        uint8_t line_key[256];
        uint32_t line_key_len, line_val_len;
        uint8_t line_val[RANGE_MAX_VALUE_LEN];

        while (pos < db->data_size && *num_results < max_results) {
            if (!decode_kv_from_block(db, pos, line_key, &line_key_len,
                                       line_val, &line_val_len, &next_off)) {
                break;
            }
            pos = next_off;

            /* Check if we've passed the range end */
            int cmp_end = key_compare(line_key, line_key_len,
                                       range->end_key, range->end_key_len);
            if (range->end_inclusive) {
                if (cmp_end > 0) break;
            } else {
                if (cmp_end >= 0) break;
            }

            /* Check if we're still below range start */
            int cmp_start = key_compare(line_key, line_key_len,
                                         range->start_key, range->start_key_len);
            if (range->start_inclusive) {
                if (cmp_start < 0) continue;
            } else {
                if (cmp_start <= 0) continue;
            }

            /* Valid entry within range */
            memcpy(results[*num_results].key, line_key, line_key_len);
            results[*num_results].key_len = line_key_len;
            memcpy(results[*num_results].value, line_val, line_val_len);
            results[*num_results].value_len = line_val_len;
            (*num_results)++;
        }
    }

    return 0;
}

/* ─────────────────────────────────────────────
   Multi-SSTable range scan with merge dedup.
   Uses a simple merge approach: scan each table individually,
   then merge-sort and dedup the combined results.
   ───────────────────────────────────────────── */
int sstable_multi_range_scan(SSTable **tables, uint32_t num_tables,
                              const KeyRange *range,
                              RangeEntry *results, uint32_t max_results,
                              uint32_t *num_results) {
    if (!tables || num_tables == 0 || !range || !results || !num_results) return -1;

    *num_results = 0;

    /* Phase 1: Collect all range entries from all tables */
    RangeEntry **per_table = (RangeEntry **)calloc(num_tables, sizeof(RangeEntry *));
    uint32_t    *per_count = (uint32_t *)calloc(num_tables, sizeof(uint32_t));
    if (!per_table || !per_count) {
        free(per_table);
        free(per_count);
        return -1;
    }

    uint32_t total_collected = 0;
    for (uint32_t t = 0; t < num_tables; t++) {
        uint32_t cap = 64;
        per_table[t] = (RangeEntry *)malloc(cap * sizeof(RangeEntry));
        uint32_t cnt = 0;

        /* Use range scan on each table */
        sstable_range_scan(tables[t], range,
                           per_table[t], cap, &cnt);
        /* Rescan with larger buffer if needed — simplified: limit to cap */
        total_collected += cnt;
        per_count[t] = cnt;
    }

    /* Phase 2: K-way merge using index pointers */
    uint32_t *indices = (uint32_t *)calloc(num_tables, sizeof(uint32_t));
    if (!indices) {
        for (uint32_t t = 0; t < num_tables; t++) free(per_table[t]);
        free(per_table); free(per_count);
        return -1;
    }

    while (*num_results < max_results) {
        /* Find smallest key among active streams */
        uint8_t  *best_key = NULL;
        uint32_t  best_key_len = 0;
        uint8_t  *best_val = NULL;
        uint32_t  best_val_len = 0;
        int       best_idx = -1;

        for (uint32_t t = 0; t < num_tables; t++) {
            if (indices[t] >= per_count[t]) continue;
            RangeEntry *e = &per_table[t][indices[t]];
            if (best_idx < 0 ||
                key_compare(e->key, e->key_len,
                            best_key, best_key_len) < 0) {
                best_key = e->key;
                best_key_len = e->key_len;
                best_val = e->value;
                best_val_len = e->value_len;
                best_idx = (int)t;
            }
        }

        if (best_idx < 0) break;

        /* Skip duplicates (same key as previous output) */
        if (*num_results > 0) {
            RangeEntry *prev = &results[*num_results - 1];
            if (prev->key_len == best_key_len &&
                memcmp(prev->key, best_key, best_key_len) == 0) {
                /* Overwrite with newer value */
                memcpy(prev->value, best_val, best_val_len);
                prev->value_len = best_val_len;
                indices[best_idx]++;
                continue;
            }
        }

        /* Output entry */
        memcpy(results[*num_results].key, best_key, best_key_len);
        results[*num_results].key_len = best_key_len;
        memcpy(results[*num_results].value, best_val, best_val_len);
        results[*num_results].value_len = best_val_len;
        (*num_results)++;
        indices[best_idx]++;
    }

    free(indices);
    for (uint32_t t = 0; t < num_tables; t++) free(per_table[t]);
    free(per_table);
    free(per_count);

    return 0;
}

/* ─────────────────────────────────────────────
   Range iterator structure
   ───────────────────────────────────────────── */
struct RangeIterator {
    SSTable         *table;
    KeyRange         range;
    uint32_t         current_block_idx;
    uint32_t         current_data_offset;
    uint32_t         num_data_blocks;
    int              exhausted;
};

/* ─────────────────────────────────────────────
   Create a streaming range iterator
   ───────────────────────────────────────────── */
RangeIterator *range_iterator_create(SSTable *table, const KeyRange *range) {
    if (!table || !range) return NULL;

    RangeIterator *iter = (RangeIterator *)calloc(1, sizeof(RangeIterator));
    if (!iter) return NULL;

    iter->table = table;
    memcpy(&iter->range, range, sizeof(KeyRange));

    /* Find the starting block */
    int start_blk = find_start_block(table, range);
    if (start_blk < 0) {
        iter->exhausted = 1;
        return iter;
    }

    iter->current_block_idx = (uint32_t)start_blk;
    iter->num_data_blocks   = table->num_data_blocks;

    /* Start at first restart point */
    if (table->data_blocks[start_blk] &&
        table->data_blocks[start_blk]->num_restarts > 0) {
        iter->current_data_offset =
            table->data_blocks[start_blk]->restart_offsets[0];
    } else {
        iter->exhausted = 1;
    }

    return iter;
}

/* ─────────────────────────────────────────────
   Get next entry from range iterator
   Returns: 1 if entry available, 0 if exhausted
   ───────────────────────────────────────────── */
int range_iterator_next(RangeIterator *iter, RangeEntry *entry_out) {
    if (!iter || iter->exhausted || !entry_out) return 0;

    while (iter->current_block_idx < iter->num_data_blocks) {
        SSTableDataBlock *db = iter->table->data_blocks[iter->current_block_idx];
        if (!db || db->num_restarts == 0) {
            iter->current_block_idx++;
            continue;
        }

        uint32_t next_off;
        uint8_t  key_buf[RANGE_MAX_KEY_LEN], val_buf[RANGE_MAX_VALUE_LEN];
        uint32_t key_len, val_len;

        if (iter->current_data_offset >= db->data_size) {
            /* Move to next block */
            iter->current_block_idx++;
            if (iter->current_block_idx < iter->num_data_blocks &&
                iter->table->data_blocks[iter->current_block_idx] &&
                iter->table->data_blocks[iter->current_block_idx]->num_restarts > 0) {
                iter->current_data_offset =
                    iter->table->data_blocks[iter->current_block_idx]->restart_offsets[0];
            }
            continue;
        }

        if (!decode_kv_from_block(db, iter->current_data_offset,
                                   key_buf, &key_len,
                                   val_buf, &val_len, &next_off)) {
            iter->current_block_idx++;
            continue;
        }
        iter->current_data_offset = next_off;

        /* Check range bounds */
        if (!key_in_range(key_buf, key_len, &iter->range)) {
            /* Check if we've passed the end */
            int cmp_end = key_compare(key_buf, key_len,
                                       iter->range.end_key, iter->range.end_key_len);
            if (!iter->range.end_inclusive && cmp_end >= 0) {
                iter->exhausted = 1;
                return 0;
            }
            if (iter->range.end_inclusive && cmp_end > 0) {
                iter->exhausted = 1;
                return 0;
            }
            continue; /* still below start, keep scanning */
        }

        /* Valid entry */
        memcpy(entry_out->key, key_buf, key_len);
        entry_out->key_len = key_len;
        memcpy(entry_out->value, val_buf, val_len);
        entry_out->value_len = val_len;
        return 1;
    }

    iter->exhausted = 1;
    return 0;
}

/* ─────────────────────────────────────────────
   Filter range scan results with a predicate
   ───────────────────────────────────────────── */
int range_filter(RangeEntry *entries, uint32_t num_entries,
                 RangeFilterFn filter, void *ctx,
                 RangeEntry *filtered, uint32_t max_filtered,
                 uint32_t *num_filtered) {
    if (!entries || !filter || !filtered || !num_filtered) return -1;

    *num_filtered = 0;
    for (uint32_t i = 0; i < num_entries && *num_filtered < max_filtered; i++) {
        if (filter(entries[i].key, entries[i].key_len,
                   entries[i].value, entries[i].value_len, ctx)) {
            memcpy(&filtered[*num_filtered], &entries[i], sizeof(RangeEntry));
            (*num_filtered)++;
        }
    }
    return 0;
}

/* ─────────────────────────────────────────────
   Destroy range iterator
   ───────────────────────────────────────────── */
void range_iterator_destroy(RangeIterator *iter) {
    free(iter);
}
