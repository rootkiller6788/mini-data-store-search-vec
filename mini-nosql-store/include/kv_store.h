#ifndef KV_STORE_H
#define KV_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define KV_MAX_KEY_LEN   64
#define KV_MAX_VALUE_LEN 256
#define KV_HASH_SLOTS    1024
#define KV_PREFIX_MAX    16

typedef struct kv_pair_t {
    char     key[KV_MAX_KEY_LEN];
    char     value[KV_MAX_VALUE_LEN];
    time_t   ttl;
    struct kv_pair_t *next;
} KVPair;

typedef struct kv_store_t {
    KVPair *hash_table[KV_HASH_SLOTS];
    int     count;
    int     use_lsm_backing;
    void   *lsm_engine;
} KVStore;

unsigned int kv_hash(const char *key);

KVStore *kv_create(int use_lsm_backing);
void     kv_destroy(KVStore *store);

int  kv_put(KVStore *store, const char *key, const char *value, time_t ttl);
int  kv_get(KVStore *store, const char *key, char *value_out, size_t max_len);
int  kv_delete(KVStore *store, const char *key);

int  kv_scan_prefix(KVStore *store, const char *prefix, KVPair **results, int max_results);
void kv_scan_free(KVPair *results, int count);

int  kv_cleanup_expired(KVStore *store);
int  kv_count(KVStore *store);

#endif
