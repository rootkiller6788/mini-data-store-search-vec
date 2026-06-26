#include "kv_store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

unsigned int kv_hash(const char *key) {
    unsigned int h = 5381;
    int c;
    while ((c = *key++))
        h = ((h << 5) + h) + c;
    return h % KV_HASH_SLOTS;
}

KVStore *kv_create(int use_lsm_backing) {
    KVStore *store = (KVStore *)calloc(1, sizeof(KVStore));
    if (!store) return NULL;
    store->use_lsm_backing = use_lsm_backing;
    store->count = 0;
    if (use_lsm_backing) {
        store->lsm_engine = NULL;
    }
    return store;
}

void kv_destroy(KVStore *store) {
    if (!store) return;
    for (int i = 0; i < KV_HASH_SLOTS; i++) {
        KVPair *cur = store->hash_table[i];
        while (cur) {
            KVPair *tmp = cur;
            cur = cur->next;
            free(tmp);
        }
    }
    free(store);
}

int kv_put(KVStore *store, const char *key, const char *value, time_t ttl) {
    if (!store || !key || !value) return -1;
    unsigned int idx = kv_hash(key);
    KVPair *cur = store->hash_table[idx];

    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            strncpy(cur->value, value, KV_MAX_VALUE_LEN - 1);
            cur->value[KV_MAX_VALUE_LEN - 1] = '\0';
            cur->ttl = ttl;
            return 0;
        }
        cur = cur->next;
    }

    KVPair *pair = (KVPair *)calloc(1, sizeof(KVPair));
    if (!pair) return -1;
    strncpy(pair->key, key, KV_MAX_KEY_LEN - 1);
    pair->key[KV_MAX_KEY_LEN - 1] = '\0';
    strncpy(pair->value, value, KV_MAX_VALUE_LEN - 1);
    pair->value[KV_MAX_VALUE_LEN - 1] = '\0';
    pair->ttl = ttl;
    pair->next = store->hash_table[idx];
    store->hash_table[idx] = pair;
    store->count++;
    return 0;
}

int kv_get(KVStore *store, const char *key, char *value_out, size_t max_len) {
    if (!store || !key || !value_out) return -1;
    unsigned int idx = kv_hash(key);
    KVPair *cur = store->hash_table[idx];
    time_t now = time(NULL);

    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            if (cur->ttl > 0 && cur->ttl < now) {
                kv_delete(store, key);
                return -2;
            }
            strncpy(value_out, cur->value, max_len - 1);
            value_out[max_len - 1] = '\0';
            return 0;
        }
        cur = cur->next;
    }
    return -2;
}

int kv_delete(KVStore *store, const char *key) {
    if (!store || !key) return -1;
    unsigned int idx = kv_hash(key);
    KVPair *cur = store->hash_table[idx];
    KVPair *prev = NULL;

    while (cur) {
        if (strcmp(cur->key, key) == 0) {
            if (prev) {
                prev->next = cur->next;
            } else {
                store->hash_table[idx] = cur->next;
            }
            free(cur);
            store->count--;
            return 0;
        }
        prev = cur;
        cur = cur->next;
    }
    return -2;
}

static int key_has_prefix(const char *key, const char *prefix) {
    return strncmp(key, prefix, strlen(prefix)) == 0;
}

int kv_scan_prefix(KVStore *store, const char *prefix, KVPair **results, int max_results) {
    if (!store || !prefix || !results) return 0;
    int found = 0;

    for (int i = 0; i < KV_HASH_SLOTS && found < max_results; i++) {
        KVPair *cur = store->hash_table[i];
        while (cur && found < max_results) {
            if (key_has_prefix(cur->key, prefix)) {
                results[found] = cur;
                found++;
            }
            cur = cur->next;
        }
    }

    if (found > 0) {
        for (int i = 0; i < found - 1; i++) {
            for (int j = i + 1; j < found; j++) {
                if (strcmp(results[i]->key, results[j]->key) > 0) {
                    KVPair *tmp = results[i];
                    results[i] = results[j];
                    results[j] = tmp;
                }
            }
        }
    }

    return found;
}

void kv_scan_free(KVPair *results, int count) {
    (void)results;
    (void)count;
}

int kv_cleanup_expired(KVStore *store) {
    if (!store) return -1;
    int cleaned = 0;
    time_t now = time(NULL);

    for (int i = 0; i < KV_HASH_SLOTS; i++) {
        KVPair *cur = store->hash_table[i];
        KVPair *prev = NULL;
        while (cur) {
            if (cur->ttl > 0 && cur->ttl < now) {
                KVPair *expired = cur;
                cur = cur->next;
                if (prev) {
                    prev->next = cur;
                } else {
                    store->hash_table[i] = cur;
                }
                free(expired);
                store->count--;
                cleaned++;
            } else {
                prev = cur;
                cur = cur->next;
            }
        }
    }
    return cleaned;
}

int kv_count(KVStore *store) {
    return store ? store->count : -1;
}
