/* ───────────────────────────────────────────────────────────
   Block Cache — LRU eviction with O(1) get/put operations.

   Architecture:
     Hash Table (open chaining) — O(1) average key lookup
     Doubly-Linked LRU List    — O(1) move-to-front / evict-tail

   Theorem (Competitive Ratio):
     LRU is k-competitive, where k is the cache size. No deterministic
     online algorithm can achieve a competitive ratio better than k.

   Reference: D.D. Sleator, R.E. Tarjan, "Amortized Efficiency of
   List Update and Paging Rules", CACM, 1985.
   ─────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block_cache.h"

/* ─────────────────────────────────────────────
   Murmur-like hash for cache key distribution
   ───────────────────────────────────────────── */
static uint32_t bcache_hash(const uint8_t *key, uint32_t len, uint32_t seed) {
    uint32_t h = seed;
    const uint32_t c1 = 0xCC9E2D51u;
    const uint32_t c2 = 0x1B873593u;
    const uint32_t r1 = 15;
    const uint32_t r2 = 13;
    const uint32_t m  = 5;
    const uint32_t n  = 0xE6546B64u;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t k = (uint32_t)key[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        h ^= k;
        h = (h << r2) | (h >> (32 - r2));
        h = h * m + n;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* ─────────────────────────────────────────────
   Create block cache with given capacity
   ───────────────────────────────────────────── */
BlockCache *bcache_create(uint32_t capacity) {
    if (capacity == 0) capacity = BCACHE_DEFAULT_CAPACITY;

    BlockCache *cache = (BlockCache *)calloc(1, sizeof(BlockCache));
    if (!cache) return NULL;

    /* Prime number of buckets for better distribution */
    cache->hash_buckets = 1021;
    if (capacity > 1021) cache->hash_buckets = 4099;

    cache->hash_table = (BCacheEntry **)calloc(cache->hash_buckets, sizeof(BCacheEntry *));
    if (!cache->hash_table) {
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->size     = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->hits     = 0;
    cache->misses   = 0;
    cache->evictions = 0;

    return cache;
}

/* ─────────────────────────────────────────────
   Internal: detach entry from LRU list
   ───────────────────────────────────────────── */
static void lru_detach(BlockCache *cache, BCacheEntry *entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        /* entry was head */
        cache->lru_head = entry->lru_next;
    }
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        /* entry was tail */
        cache->lru_tail = entry->lru_prev;
    }
}

/* ─────────────────────────────────────────────
   Internal: attach entry to LRU head (MRU position)
   ───────────────────────────────────────────── */
static void lru_attach_head(BlockCache *cache, BCacheEntry *entry) {
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;
    if (cache->lru_head) {
        cache->lru_head->lru_prev = entry;
    } else {
        cache->lru_tail = entry;
    }
    cache->lru_head = entry;
}

/* ─────────────────────────────────────────────
   Internal: evict LRU tail entry
   ───────────────────────────────────────────── */
static void lru_evict_tail(BlockCache *cache) {
    if (!cache->lru_tail) return;

    BCacheEntry *victim = cache->lru_tail;
    lru_detach(cache, victim);

    /* Remove from hash table */
    uint32_t h = bcache_hash(victim->key, victim->key_len, 0x517CC1B7u);
    uint32_t bucket = h % cache->hash_buckets;
    BCacheEntry **pp = &cache->hash_table[bucket];
    while (*pp) {
        if (*pp == victim) {
            *pp = victim->hash_next;
            break;
        }
        pp = &(*pp)->hash_next;
    }

    free(victim->data);
    free(victim);
    cache->size--;
    cache->evictions++;
}

/* ─────────────────────────────────────────────
   Internal: find entry in hash table by key
   ───────────────────────────────────────────── */
static BCacheEntry *bcache_find(BlockCache *cache,
                                 const uint8_t *key, uint32_t key_len) {
    uint32_t h = bcache_hash(key, key_len, 0x517CC1B7u);
    uint32_t bucket = h % cache->hash_buckets;

    BCacheEntry *entry = cache->hash_table[bucket];
    while (entry) {
        if (entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   Put a data block into the cache
   ───────────────────────────────────────────── */
int bcache_put(BlockCache *cache,
               const uint8_t *key, uint32_t key_len,
               const uint8_t *data, uint32_t data_size) {
    if (!cache || !key || key_len > BCACHE_MAX_KEY_SIZE || !data) return -1;

    /* Check if key already exists — update in place */
    BCacheEntry *existing = bcache_find(cache, key, key_len);
    if (existing) {
        free(existing->data);
        existing->data = (uint8_t *)malloc(data_size);
        if (!existing->data) return -1;
        memcpy(existing->data, data, data_size);
        existing->data_size = data_size;
        existing->access_timestamp++;
        existing->access_count++;
        /* Move to MRU */
        lru_detach(cache, existing);
        lru_attach_head(cache, existing);
        return 0;
    }

    /* Evict if at capacity */
    while (cache->size >= cache->capacity) {
        lru_evict_tail(cache);
    }

    /* Allocate new entry */
    BCacheEntry *entry = (BCacheEntry *)calloc(1, sizeof(BCacheEntry));
    if (!entry) return -1;

    memcpy(entry->key, key, key_len);
    entry->key_len  = key_len;
    entry->data     = (uint8_t *)malloc(data_size);
    if (!entry->data) {
        free(entry);
        return -1;
    }
    memcpy(entry->data, data, data_size);
    entry->data_size      = data_size;
    entry->access_count   = 1;
    entry->access_timestamp = 1;

    /* Insert into hash table */
    uint32_t h = bcache_hash(key, key_len, 0x517CC1B7u);
    uint32_t bucket = h % cache->hash_buckets;
    entry->hash_next = cache->hash_table[bucket];
    cache->hash_table[bucket] = entry;

    /* Insert into LRU list (at head / MRU) */
    lru_attach_head(cache, entry);
    cache->size++;

    return 0;
}

/* ─────────────────────────────────────────────
   Get a data block from the cache
   Returns: 1 on hit, 0 on miss, -1 on error
   ───────────────────────────────────────────── */
int bcache_get(BlockCache *cache,
               const uint8_t *key, uint32_t key_len,
               uint8_t *data_out, uint32_t *data_size_out) {
    if (!cache || !key || !data_out || !data_size_out) return -1;

    BCacheEntry *entry = bcache_find(cache, key, key_len);
    if (!entry) {
        cache->misses++;
        return 0; /* miss */
    }

    /* Hit: move to MRU position */
    lru_detach(cache, entry);
    lru_attach_head(cache, entry);
    entry->access_count++;
    entry->access_timestamp++;

    memcpy(data_out, entry->data, entry->data_size);
    *data_size_out = entry->data_size;
    cache->hits++;
    return 1;
}

/* ─────────────────────────────────────────────
   Invalidate / remove a specific entry
   ───────────────────────────────────────────── */
int bcache_invalidate(BlockCache *cache,
                      const uint8_t *key, uint32_t key_len) {
    if (!cache || !key) return -1;

    BCacheEntry *entry = bcache_find(cache, key, key_len);
    if (!entry) return 0;

    lru_detach(cache, entry);

    /* Remove from hash table */
    uint32_t h = bcache_hash(key, key_len, 0x517CC1B7u);
    uint32_t bucket = h % cache->hash_buckets;
    BCacheEntry **pp = &cache->hash_table[bucket];
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            break;
        }
        pp = &(*pp)->hash_next;
    }

    free(entry->data);
    free(entry);
    cache->size--;

    return 1;
}

/* ─────────────────────────────────────────────
   Get cache statistics
   ───────────────────────────────────────────── */
void bcache_get_stats(BlockCache *cache, BCacheStats *stats) {
    if (!cache || !stats) return;
    stats->hits      = cache->hits;
    stats->misses    = cache->misses;
    stats->evictions = cache->evictions;
    stats->size      = cache->size;
    stats->capacity  = cache->capacity;
    uint64_t total   = cache->hits + cache->misses;
    stats->hit_rate  = (total > 0) ? (double)cache->hits / (double)total : 0.0;
}

/* ─────────────────────────────────────────────
   Clear all entries from the cache
   ───────────────────────────────────────────── */
void bcache_clear(BlockCache *cache) {
    if (!cache) return;

    /* Walk LRU list and free all entries */
    BCacheEntry *entry = cache->lru_head;
    while (entry) {
        BCacheEntry *next = entry->lru_next;
        free(entry->data);
        free(entry);
        entry = next;
    }

    /* Reset hash table */
    memset(cache->hash_table, 0, cache->hash_buckets * sizeof(BCacheEntry *));
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->size = 0;
}

/* ─────────────────────────────────────────────
   Destroy the entire cache
   ───────────────────────────────────────────── */
void bcache_destroy(BlockCache *cache) {
    if (!cache) return;
    bcache_clear(cache);
    free(cache->hash_table);
    free(cache);
}
