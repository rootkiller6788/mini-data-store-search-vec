#ifndef MINI_BLOCK_CACHE_H
#define MINI_BLOCK_CACHE_H

#include <stdint.h>
#include <stddef.h>

/* ─────────────────────────────────────────────
   Block Cache — LRU eviction for SSTable data blocks

   Theorem (Belady's Optimal):
     The optimal offline page replacement algorithm evicts the page
     that will be used furthest in the future. LRU approximates this
     by evicting the least recently used page.

   Complexity: O(1) for both get() and put() using a hash table
   combined with a doubly-linked list.

   Reference: L.A. Belady, "A Study of Replacement Algorithms
   for Virtual-Storage Computer", IBM Systems Journal, 1966.
   ───────────────────────────────────────────── */

#define BCACHE_DEFAULT_CAPACITY 128
#define BCACHE_MAX_KEY_SIZE     256

/* ─────────────────────────────────────────────
   Cached block entry
   ───────────────────────────────────────────── */
typedef struct BCacheEntry {
    uint8_t             key[BCACHE_MAX_KEY_SIZE];
    uint32_t            key_len;
    uint8_t            *data;
    uint32_t            data_size;
    uint64_t            access_count;
    uint64_t            access_timestamp;
    struct BCacheEntry *hash_next;   /* next in hash bucket */
    struct BCacheEntry *lru_prev;    /* doubly-linked LRU list */
    struct BCacheEntry *lru_next;
} BCacheEntry;

/* ─────────────────────────────────────────────
   Block cache handle
   ───────────────────────────────────────────── */
typedef struct {
    BCacheEntry **hash_table;
    uint32_t      hash_buckets;
    BCacheEntry  *lru_head;          /* most recently used */
    BCacheEntry  *lru_tail;          /* least recently used */
    uint32_t      capacity;
    uint32_t      size;
    uint64_t      hits;
    uint64_t      misses;
    uint64_t      evictions;
} BlockCache;

/* ─────────────────────────────────────────────
   Cache statistics
   ───────────────────────────────────────────── */
typedef struct {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint32_t size;
    uint32_t capacity;
    double   hit_rate;
} BCacheStats;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

BlockCache *bcache_create(uint32_t capacity);

int  bcache_put(BlockCache *cache,
                const uint8_t *key, uint32_t key_len,
                const uint8_t *data, uint32_t data_size);

int  bcache_get(BlockCache *cache,
                const uint8_t *key, uint32_t key_len,
                uint8_t *data_out, uint32_t *data_size_out);

int  bcache_invalidate(BlockCache *cache,
                       const uint8_t *key, uint32_t key_len);

void bcache_get_stats(BlockCache *cache, BCacheStats *stats_out);

void bcache_clear(BlockCache *cache);

void bcache_destroy(BlockCache *cache);

#endif /* MINI_BLOCK_CACHE_H */
