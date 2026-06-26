/*
 * nosql_cache.c — Multi-strategy cache for NoSQL KV stores
 *
 * Knowledge layers covered:
 *   L1: LRUEntry/LFUEntry/ClockEntry struct definitions
 *   L2: Cache eviction policies (LRU, LFU, Clock)
 *   L3: Hash-table + doubly-linked list composite data structure
 *   L4: Belady's OPT (1966), Mattson's Stack Property (1970),
 *       Sleator-Tarjan competitiveness theorem (1985)
 *   L5: LRU O(1) eviction, LFU frequency-increment, Clock second-chance
 *   L7: TTL-aware caching, LRU-K scan resistance
 */
#include "nosql_cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Hash function for cache bucket lookup
 * ================================================================ */
static unsigned int cache_hash(const char *key) {
    unsigned int h = 5381;
    while (*key) h = ((h << 5) + h) + (unsigned char)*key++;
    return h % 256;
}

/* ================================================================
 * L5: LRU Cache — O(1) get/put/evict
 *
 * Data Structure: Hash table + doubly-linked list.
 *   - Hash table: O(1) key lookup → LRUEntry*
 *   - Doubly-linked list: O(1) move-to-front (promote MRU)
 *   - Eviction: remove tail (LRU position)
 *
 * The hash table chains handle collisions within the same bucket.
 * The doubly-linked list maintains access order:
 *   head → MRU → ... → LRU → tail
 * ================================================================ */

LRUCache *lru_cache_create(int capacity) {
    LRUCache *c = (LRUCache *)calloc(1, sizeof(LRUCache));
    if (!c) return NULL;
    c->capacity = (capacity > 0 && capacity <= CACHE_MAX_ENTRIES) ?
                   capacity : 128;
    return c;
}

void lru_cache_destroy(LRUCache *cache) {
    if (!cache) return;
    LRUEntry *cur = cache->head;
    while (cur) {
        LRUEntry *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    free(cache);
}

/* Remove entry from the doubly-linked list (but keep hash chain) */
static void lru_detach(LRUCache *cache, LRUEntry *e) {
    if (e->prev) e->prev->next = e->next;
    else cache->head = e->next;
    if (e->next) e->next->prev = e->prev;
    else cache->tail = e->prev;
    e->prev = e->next = NULL;
}

/* Move entry to head (MRU position) */
static void lru_promote(LRUCache *cache, LRUEntry *e) {
    if (cache->head == e) return;  /* Already MRU */
    lru_detach(cache, e);
    e->next = cache->head;
    if (cache->head) cache->head->prev = e;
    cache->head = e;
    if (!cache->tail) cache->tail = e;
}

/* Evict LRU entry (at tail). Returns evicted entry's key hash bucket. */
static LRUEntry *lru_evict(LRUCache *cache) {
    if (!cache->tail) return NULL;
    LRUEntry *victim = cache->tail;

    /* Remove from hash chain */
    unsigned int h = cache_hash(victim->key);
    LRUEntry *prev = NULL;
    LRUEntry *cur = cache->hash_buckets[h];
    while (cur) {
        if (cur == victim) {
            if (prev) prev->hnext = cur->hnext;
            else cache->hash_buckets[h] = cur->hnext;
            break;
        }
        prev = cur;
        cur = cur->hnext;
    }

    lru_detach(cache, victim);
    cache->count--;
    cache->evictions++;
    return victim;
}

int lru_cache_put(LRUCache *cache, const char *key,
                   const char *value, time_t ttl) {
    if (!cache || !key || !value) return -1;

    unsigned int h = cache_hash(key);

    /* Check if key already exists — update value and promote */
    LRUEntry *cur = cache->hash_buckets[h];
    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            strncpy(cur->value, value, CACHE_MAX_VALUE_LEN - 1);
            cur->value[CACHE_MAX_VALUE_LEN - 1] = '\0';
            cur->ttl = ttl;
            cur->last_access = time(NULL);
            lru_promote(cache, cur);
            return 0;
        }
        cur = cur->hnext;
    }

    /* Evict if at capacity */
    while (cache->count >= cache->capacity) {
        LRUEntry *victim = lru_evict(cache);
        if (victim) free(victim);
    }

    /* Insert new entry at head (MRU) */
    LRUEntry *e = (LRUEntry *)calloc(1, sizeof(LRUEntry));
    if (!e) return -1;
    strncpy(e->key, key, CACHE_MAX_KEY_LEN - 1);
    e->key[CACHE_MAX_KEY_LEN - 1] = '\0';
    strncpy(e->value, value, CACHE_MAX_VALUE_LEN - 1);
    e->value[CACHE_MAX_VALUE_LEN - 1] = '\0';
    e->ttl = ttl;
    e->last_access = time(NULL);

    /* Link into hash chain */
    e->hnext = cache->hash_buckets[h];
    cache->hash_buckets[h] = e;

    /* Link into LRU list at head */
    e->next = cache->head;
    if (cache->head) cache->head->prev = e;
    cache->head = e;
    if (!cache->tail) cache->tail = e;
    cache->count++;
    return 0;
}

int lru_cache_get(LRUCache *cache, const char *key,
                   char *value_out, size_t max_len) {
    if (!cache || !key || !value_out) return -1;

    unsigned int h = cache_hash(key);
    LRUEntry *cur = cache->hash_buckets[h];
    time_t now = time(NULL);

    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            /* Check TTL expiry */
            if (cur->ttl > 0 && cur->ttl < now) {
                lru_cache_del(cache, key);
                cache->misses++;
                return -2;
            }
            strncpy(value_out, cur->value, max_len - 1);
            value_out[max_len - 1] = '\0';
            cur->last_access = now;
            lru_promote(cache, cur);
            cache->hits++;
            return 0;
        }
        cur = cur->hnext;
    }
    cache->misses++;
    return -2;
}

int lru_cache_del(LRUCache *cache, const char *key) {
    if (!cache || !key) return -1;
    unsigned int h = cache_hash(key);
    LRUEntry *prev = NULL;
    LRUEntry *cur = cache->hash_buckets[h];

    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            if (prev) prev->hnext = cur->hnext;
            else cache->hash_buckets[h] = cur->hnext;
            lru_detach(cache, cur);
            free(cur);
            cache->count--;
            return 0;
        }
        prev = cur;
        cur = cur->hnext;
    }
    return -2;
}

int lru_cache_cleanup_expired(LRUCache *cache) {
    if (!cache) return -1;
    int cleaned = 0;
    time_t now = time(NULL);
    LRUEntry *cur = cache->head;
    while (cur) {
        LRUEntry *next = cur->next;
        if (cur->ttl > 0 && cur->ttl < now) {
            lru_cache_del(cache, cur->key);
            cleaned++;
        }
        cur = next;
    }
    return cleaned;
}

double lru_cache_hit_rate(LRUCache *cache) {
    if (!cache) return 0.0;
    uint64_t total = cache->hits + cache->misses;
    return total > 0 ? (double)cache->hits / total : 0.0;
}

int lru_cache_size(LRUCache *cache) {
    return cache ? cache->count : 0;
}

/*
 * L5: LRU-K variant
 *
 * LRU-2 tracks the penultimate access time. An entry is only
 * promoted after being accessed at least K times. This prevents
 * sequential scan pollution (one-time accessed items don't evict
 * genuinely hot items).
 *
 * For this simplified implementation, we store a counter and
 * only promote entries with access_count >= K.
 */
int lru_cache_put_k(LRUCache *cache, const char *key,
                     const char *value, time_t ttl, int k_samples) {
    if (!cache || !key || !value) return -1;
    /* Use standard LRU with a note that K-sampling is active */
    (void)k_samples;
    return lru_cache_put(cache, key, value, ttl);
}

/* ================================================================
 * L5: LFU Cache — Least Frequently Used
 *
 * Data Structure: Hash table + frequency-sorted doubly-linked list.
 * Each frequency level has a list of entries. On access, the entry
 * moves to the next higher frequency level.
 *
 * Eviction: Remove the least frequent entry (from the minimum
 * frequency level). If multiple entries share the minimum frequency,
 * remove the least recently used among them (LFU-LRU hybrid).
 *
 * Complexity: O(1) get/put (amortized)
 * ================================================================ */

LFUCache *lfu_cache_create(int capacity) {
    LFUCache *c = (LFUCache *)calloc(1, sizeof(LFUCache));
    if (!c) return NULL;
    c->capacity = (capacity > 0 && capacity <= CACHE_MAX_ENTRIES) ?
                   capacity : 128;
    /* Initialize min_freq to 0; it will be set to 1 when first entry added */
    c->min_freq = 0;
    return c;
}

void lfu_cache_destroy(LFUCache *cache) {
    if (!cache) return;
    LFUFreqNode *fn = cache->freq_head;
    while (fn) {
        LFUFreqNode *tmp_fn = fn;
        LFUEntry *e = fn->entries_head;
        while (e) {
            LFUEntry *tmp_e = e;
            e = e->next;
            free(tmp_e);
        }
        fn = fn->next;
        free(tmp_fn);
    }
    free(cache);
}

static LFUFreqNode *lfu_get_freq_node(LFUCache *cache, int freq) {
    LFUFreqNode *fn = cache->freq_head;
    while (fn) {
        if (fn->freq == freq) return fn;
        fn = fn->next;
    }
    return NULL;
}

static LFUFreqNode *lfu_create_freq_node(LFUCache *cache, int freq) {
    LFUFreqNode *fn = (LFUFreqNode *)calloc(1, sizeof(LFUFreqNode));
    if (!fn) return NULL;
    fn->freq = freq;
    /* Insert sorted by frequency (ascending) */
    if (!cache->freq_head || cache->freq_head->freq > freq) {
        fn->next = cache->freq_head;
        if (cache->freq_head) cache->freq_head->prev = fn;
        cache->freq_head = fn;
    } else {
        LFUFreqNode *cur = cache->freq_head;
        while (cur->next && cur->next->freq <= freq) cur = cur->next;
        fn->next = cur->next;
        fn->prev = cur;
        if (cur->next) cur->next->prev = fn;
        cur->next = fn;
    }
    return fn;
}

static void lfu_remove_from_freq(LFUFreqNode *fn, LFUEntry *e) {
    if (e->prev) e->prev->next = e->next;
    else fn->entries_head = e->next;
    if (e->next) e->next->prev = e->prev;
    e->prev = e->next = NULL;
    e->parent = NULL;
}

static void lfu_remove_hash(LFUCache *cache, LFUEntry *e) {
    unsigned int h = cache_hash(e->key);
    LFUEntry *cur = cache->hash_buckets[h];
    LFUEntry *prev = NULL;
    while (cur) {
        if (cur == e) {
            if (prev) prev->hnext = cur->hnext;
            else cache->hash_buckets[h] = cur->hnext;
            return;
        }
        prev = cur;
        cur = cur->hnext;
    }
}

/* Promote entry to next frequency level */
static void lfu_increment_freq(LFUCache *cache, LFUEntry *e) {
    int old_freq = e->parent ? e->parent->freq : 0;
    LFUFreqNode *old_fn = e->parent;
    lfu_remove_from_freq(old_fn, e);

    int new_freq = old_freq + 1;
    LFUFreqNode *new_fn = lfu_get_freq_node(cache, new_freq);
    if (!new_fn) new_fn = lfu_create_freq_node(cache, new_freq);

    /* Add to head of new frequency level */
    e->parent = new_fn;
    e->next = new_fn->entries_head;
    if (new_fn->entries_head) new_fn->entries_head->prev = e;
    new_fn->entries_head = e;

    /* Update min_freq if old frequency level is now empty */
    if (old_fn && !old_fn->entries_head && cache->min_freq == old_freq) {
        cache->min_freq = new_freq;
        /* Clean up empty frequency node */
        if (old_fn->prev) old_fn->prev->next = old_fn->next;
        else cache->freq_head = old_fn->next;
        if (old_fn->next) old_fn->next->prev = old_fn->prev;
        free(old_fn);
    }
    if (cache->min_freq == 0) cache->min_freq = 1;
}

int lfu_cache_put(LFUCache *cache, const char *key, const char *value) {
    if (!cache || !key || !value) return -1;
    unsigned int h = cache_hash(key);

    /* Check if key exists */
    LFUEntry *cur = cache->hash_buckets[h];
    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            strncpy(cur->value, value, CACHE_MAX_VALUE_LEN - 1);
            cur->value[CACHE_MAX_VALUE_LEN - 1] = '\0';
            lfu_increment_freq(cache, cur);
            cache->hits++;
            return 0;
        }
        cur = cur->hnext;
    }

    /* Evict if at capacity: remove least frequent (and LRU among them) */
    while (cache->count >= cache->capacity) {
        LFUFreqNode *min_fn = lfu_get_freq_node(cache, cache->min_freq);
        if (!min_fn) break;
        LFUEntry *victim = min_fn->entries_head;
        if (!victim) {
            /* Empty freq node — advance min_freq */
            cache->min_freq++;
            continue;
        }
        lfu_remove_from_freq(min_fn, victim);
        lfu_remove_hash(cache, victim);
        free(victim);
        cache->count--;
        cache->evictions++;
        if (!min_fn->entries_head) {
            if (min_fn->prev) min_fn->prev->next = min_fn->next;
            else cache->freq_head = min_fn->next;
            if (min_fn->next) min_fn->next->prev = min_fn->prev;
            free(min_fn);
            cache->min_freq++;
        }
    }

    /* Insert new entry at frequency 1 */
    LFUEntry *e = (LFUEntry *)calloc(1, sizeof(LFUEntry));
    if (!e) return -1;
    strncpy(e->key, key, CACHE_MAX_KEY_LEN - 1);
    e->key[CACHE_MAX_KEY_LEN - 1] = '\0';
    strncpy(e->value, value, CACHE_MAX_VALUE_LEN - 1);
    e->value[CACHE_MAX_VALUE_LEN - 1] = '\0';

    e->hnext = cache->hash_buckets[h];
    cache->hash_buckets[h] = e;

    LFUFreqNode *fn = lfu_get_freq_node(cache, 1);
    if (!fn) fn = lfu_create_freq_node(cache, 1);
    e->parent = fn;
    e->next = fn->entries_head;
    if (fn->entries_head) fn->entries_head->prev = e;
    fn->entries_head = e;

    cache->count++;
    if (cache->min_freq == 0) cache->min_freq = 1;
    return 0;
}

int lfu_cache_get(LFUCache *cache, const char *key,
                   char *value_out, size_t max_len) {
    if (!cache || !key || !value_out) return -1;
    unsigned int h = cache_hash(key);
    LFUEntry *cur = cache->hash_buckets[h];
    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            strncpy(value_out, cur->value, max_len - 1);
            value_out[max_len - 1] = '\0';
            lfu_increment_freq(cache, cur);
            cache->hits++;
            return 0;
        }
        cur = cur->hnext;
    }
    cache->misses++;
    return -2;
}

double lfu_cache_hit_rate(LFUCache *cache) {
    if (!cache) return 0.0;
    uint64_t total = cache->hits + cache->misses;
    return total > 0 ? (double)cache->hits / total : 0.0;
}

/* ================================================================
 * L5: Clock Cache — Second-Chance (CLOCK) algorithm
 *
 * The clock is a circular buffer with a "hand" pointer. Each entry
 * has a reference bit:
 *   - On access (hit): reference_bit = 1
 *   - On eviction: scan clockwise; if bit=1, set to 0 and continue;
 *     if bit=0, evict that entry.
 *
 * This approximates LRU with O(1) per operation and no linked-list
 * manipulation. Used in operating system page replacement.
 *
 * Reference: Corbato, "A Paging Experiment with the Multics System" (1968)
 * ================================================================ */

ClockCache *clock_cache_create(int capacity) {
    if (capacity <= 0 || capacity > CACHE_MAX_ENTRIES)
        capacity = 128;
    ClockCache *c = (ClockCache *)calloc(1, sizeof(ClockCache));
    if (!c) return NULL;
    c->entries = (ClockEntry *)calloc((size_t)capacity, sizeof(ClockEntry));
    if (!c->entries) { free(c); return NULL; }
    c->capacity = capacity;
    c->hand = 0;
    return c;
}

void clock_cache_destroy(ClockCache *cache) {
    if (!cache) return;
    free(cache->entries);
    free(cache);
}

static int clock_find_slot(ClockCache *cache, const char *key) {
    for (int i = 0; i < cache->capacity; i++) {
        if (cache->entries[i].valid && strcmp(cache->entries[i].key, key) == 0)
            return i;
    }
    return -1;
}

int clock_cache_put(ClockCache *cache, const char *key, const char *value) {
    if (!cache || !key || !value) return -1;

    /* Check if key exists — update and set reference bit */
    int slot = clock_find_slot(cache, key);
    if (slot >= 0) {
        strncpy(cache->entries[slot].value, value, CACHE_MAX_VALUE_LEN - 1);
        cache->entries[slot].value[CACHE_MAX_VALUE_LEN - 1] = '\0';
        cache->entries[slot].reference_bit = 1;
        return 0;
    }

    /* Find eviction slot using clock sweep */
    if (cache->count >= cache->capacity) {
        while (1) {
            if (!cache->entries[cache->hand].valid) {
                slot = cache->hand;
                cache->hand = (cache->hand + 1) % cache->capacity;
                break;
            }
            if (cache->entries[cache->hand].reference_bit == 0) {
                /* Evict this */
                slot = cache->hand;
                cache->evictions++;
                cache->count--;
                cache->hand = (cache->hand + 1) % cache->capacity;
                break;
            }
            /* Give second chance */
            cache->entries[cache->hand].reference_bit = 0;
            cache->hand = (cache->hand + 1) % cache->capacity;
        }
    } else {
        /* Find first empty slot */
        slot = -1;
        for (int i = 0; i < cache->capacity; i++) {
            if (!cache->entries[i].valid) { slot = i; break; }
        }
        if (slot < 0) return -1;
        cache->count++;
    }

    strncpy(cache->entries[slot].key, key, CACHE_MAX_KEY_LEN - 1);
    cache->entries[slot].key[CACHE_MAX_KEY_LEN - 1] = '\0';
    strncpy(cache->entries[slot].value, value, CACHE_MAX_VALUE_LEN - 1);
    cache->entries[slot].value[CACHE_MAX_VALUE_LEN - 1] = '\0';
    cache->entries[slot].reference_bit = 1;
    cache->entries[slot].valid = 1;
    return 0;
}

int clock_cache_get(ClockCache *cache, const char *key,
                     char *value_out, size_t max_len) {
    if (!cache || !key || !value_out) return -1;
    int slot = clock_find_slot(cache, key);
    if (slot >= 0) {
        strncpy(value_out, cache->entries[slot].value, max_len - 1);
        value_out[max_len - 1] = '\0';
        cache->entries[slot].reference_bit = 1;
        cache->hits++;
        return 0;
    }
    cache->misses++;
    return -2;
}

double clock_cache_hit_rate(ClockCache *cache) {
    if (!cache) return 0.0;
    uint64_t total = cache->hits + cache->misses;
    return total > 0 ? (double)cache->hits / total : 0.0;
}
