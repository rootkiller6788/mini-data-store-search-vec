#ifndef NOSQL_CACHE_H
#define NOSQL_CACHE_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/*
 * Multi-Strategy Cache Engine for NoSQL KV store
 *
 * Caching is critical for NoSQL performance. This module implements
 * three classic cache eviction policies:
 *   - LRU  (Least Recently Used)   — O(1) with hash+linked list
 *   - LFU  (Least Frequently Used) — frequency-based eviction
 *   - Clock (Second-Chance / CLOCK-Pro) — page-replacement style
 *
 * Theorem (Belady's OPT, 1966):
 *   The optimal page replacement algorithm evicts the page that will
 *   be used furthest in the future. LRU is the best online approximation
 *   under the Stack Property (Mattson et al., 1970).
 *
 * Theorem (LRU Competitiveness, Sleator-Tarjan 1985):
 *   LRU is k-competitive for paging (k = cache size). No deterministic
 *   online algorithm has ratio < k.
 */

#define CACHE_MAX_KEY_LEN   64
#define CACHE_MAX_VALUE_LEN 256
#define CACHE_MAX_ENTRIES  512

/* ---------- LRU Cache (L5: O(1) eviction) ---------- */
typedef struct lru_entry_t {
    char    key[CACHE_MAX_KEY_LEN];
    char    value[CACHE_MAX_VALUE_LEN];
    time_t  ttl;              /* 0 = no expiry */
    time_t  last_access;
    struct lru_entry_t *prev;
    struct lru_entry_t *next;
    struct lru_entry_t *hnext;  /* Hash bucket chain */
} LRUEntry;

typedef struct lru_cache_t {
    LRUEntry *hash_buckets[256];
    LRUEntry *head;           /* MRU (most recently used) */
    LRUEntry *tail;           /* LRU (least recently used) */
    int       capacity;
    int       count;
    uint64_t  hits;
    uint64_t  misses;
    uint64_t  evictions;
} LRUCache;

LRUCache *lru_cache_create(int capacity);
void      lru_cache_destroy(LRUCache *cache);
int       lru_cache_put(LRUCache *cache, const char *key,
                        const char *value, time_t ttl);
int       lru_cache_get(LRUCache *cache, const char *key,
                        char *value_out, size_t max_len);
int       lru_cache_del(LRUCache *cache, const char *key);
int       lru_cache_cleanup_expired(LRUCache *cache);
double    lru_cache_hit_rate(LRUCache *cache);
int       lru_cache_size(LRUCache *cache);

/*
 * L5: LRU-K variant — tracks last K accesses instead of just one.
 * LRU-2 provides better scan resistance: sequential scans don't
 * pollute the cache (O'Neil, SIGMOD 1993).
 */
int lru_cache_put_k(LRUCache *cache, const char *key,
                    const char *value, time_t ttl, int k_samples);

/* ---------- LFU Cache (L5: Frequency-based) ---------- */
typedef struct lfu_freq_node_t {
    int    freq;
    struct lfu_entry_t *entries_head;
    struct lfu_freq_node_t *prev;
    struct lfu_freq_node_t *next;
} LFUFreqNode;

typedef struct lfu_entry_t {
    char    key[CACHE_MAX_KEY_LEN];
    char    value[CACHE_MAX_VALUE_LEN];
    struct lfu_freq_node_t *parent;
    struct lfu_entry_t *prev;
    struct lfu_entry_t *next;
    struct lfu_entry_t *hnext;  /* Hash chain */
} LFUEntry;

typedef struct lfu_cache_t {
    LFUEntry    *hash_buckets[256];
    LFUFreqNode *freq_head;   /* Frequency list head */
    int          capacity;
    int          count;
    int          min_freq;
    uint64_t     hits;
    uint64_t     misses;
    uint64_t     evictions;
} LFUCache;

LFUCache *lfu_cache_create(int capacity);
void      lfu_cache_destroy(LFUCache *cache);
int       lfu_cache_put(LFUCache *cache, const char *key, const char *value);
int       lfu_cache_get(LFUCache *cache, const char *key,
                        char *value_out, size_t max_len);
double    lfu_cache_hit_rate(LFUCache *cache);

/* ---------- Clock Cache (L5: Second-Chance) ---------- */
typedef struct clock_entry_t {
    char    key[CACHE_MAX_KEY_LEN];
    char    value[CACHE_MAX_VALUE_LEN];
    int     reference_bit;    /* 1 = recently referenced */
    int     valid;
} ClockEntry;

typedef struct clock_cache_t {
    ClockEntry *entries;
    int         capacity;
    int         count;
    int         hand;         /* Clock hand position */
    uint64_t    hits;
    uint64_t    misses;
    uint64_t    evictions;
} ClockCache;

ClockCache *clock_cache_create(int capacity);
void        clock_cache_destroy(ClockCache *cache);
int         clock_cache_put(ClockCache *cache, const char *key, const char *value);
int         clock_cache_get(ClockCache *cache, const char *key,
                            char *value_out, size_t max_len);
double      clock_cache_hit_rate(ClockCache *cache);

#endif
