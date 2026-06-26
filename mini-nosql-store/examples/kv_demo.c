#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kv_store.h"

int main(void) {
    printf("=== mini-nosql KV Store Demo ===\n\n");

    KVStore *store = kv_create(0);
    if (!store) {
        fprintf(stderr, "Failed to create KV store\n");
        return 1;
    }

    kv_put(store, "user:1", "Alice", 0);
    kv_put(store, "user:2", "Bob", 0);
    kv_put(store, "user:3", "Charlie", 0);
    kv_put(store, "product:100", "Laptop", 0);
    kv_put(store, "product:101", "Mouse", 0);
    kv_put(store, "product:102", "Keyboard", 0);
    kv_put(store, "session:abc", "active", time(NULL) + 1);

    printf("Store count: %d\n\n", kv_count(store));

    char val[KV_MAX_VALUE_LEN];
    if (kv_get(store, "user:1", val, sizeof(val)) == 0)
        printf("GET user:1 -> %s\n", val);
    if (kv_get(store, "product:100", val, sizeof(val)) == 0)
        printf("GET product:100 -> %s\n", val);

    printf("\n--- Prefix scan 'user:' ---\n");
    KVPair *results[16];
    int n = kv_scan_prefix(store, "user:", results, 16);
    for (int i = 0; i < n; i++)
        printf("  %s = %s\n", results[i]->key, results[i]->value);

    printf("\n--- Prefix scan 'product:' ---\n");
    n = kv_scan_prefix(store, "product:", results, 16);
    for (int i = 0; i < n; i++)
        printf("  %s = %s\n", results[i]->key, results[i]->value);

    printf("\n--- Test TTL expiry ---\n");
    printf("Waiting 2 seconds for session TTL...\n");
    fflush(stdout);

    {
        time_t start = time(NULL);
        while (time(NULL) - start < 3) { /* busy-wait ~3 seconds */ }
    }

    int cleaned = kv_cleanup_expired(store);
    printf("Expired entries cleaned: %d\n", cleaned);
    printf("Store count after cleanup: %d\n", kv_count(store));

    int rc = kv_get(store, "session:abc", val, sizeof(val));
    printf("GET session:abc -> %s (expired)\n", rc == -2 ? "NOT FOUND" : val);

    printf("\n--- Delete test ---\n");
    kv_delete(store, "user:2");
    rc = kv_get(store, "user:2", val, sizeof(val));
    printf("GET user:2 after delete -> %s\n", rc == -2 ? "NOT FOUND" : val);
    printf("Store count: %d\n", kv_count(store));

    kv_destroy(store);
    printf("\nKV store demo complete.\n");
    return 0;
}
