#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "nosql_cache.h"

int main(void) {
    /* LRU Cache */
    LRUCache *lru = lru_cache_create(64);
    assert(lru != NULL);

    assert(lru_cache_put(lru, "a", "1", 0) == 0);
    assert(lru_cache_put(lru, "b", "2", 0) == 0);
    assert(lru_cache_size(lru) == 2);

    char buf[256];
    assert(lru_cache_get(lru, "a", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "1") == 0);
    assert(lru_cache_get(lru, "x", buf, sizeof(buf)) == -2);

    /* Test eviction: fill cache */
    for (int i = 0; i < 200; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "k%d", i);
        snprintf(v, sizeof(v), "v%d", i);
        lru_cache_put(lru, k, v, 0);
    }
    assert(lru_cache_size(lru) == 64);

    /* TTL test */
    assert(lru_cache_put(lru, "ttlkey", "ttlval", time(NULL) - 1) == 0);
    assert(lru_cache_get(lru, "ttlkey", buf, sizeof(buf)) == -2);

    int cleaned = lru_cache_cleanup_expired(lru);
    assert(cleaned >= 0);

    double hr = lru_cache_hit_rate(lru);
    assert(hr >= 0.0 && hr <= 1.0);

    lru_cache_destroy(lru);

    /* LFU Cache */
    LFUCache *lfu = lfu_cache_create(32);
    assert(lfu != NULL);
    assert(lfu_cache_put(lfu, "hot", "hotval") == 0);
    assert(lfu_cache_get(lfu, "hot", buf, sizeof(buf)) == 0);
    assert(lfu_cache_get(lfu, "hot", buf, sizeof(buf)) == 0);
    assert(lfu_cache_get(lfu, "hot", buf, sizeof(buf)) == 0);
    assert(lfu_cache_hit_rate(lfu) > 0.0);

    for (int i = 0; i < 100; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "lfu%d", i);
        snprintf(v, sizeof(v), "lv%d", i);
        lfu_cache_put(lfu, k, v);
    }
    lfu_cache_destroy(lfu);

    /* Clock Cache */
    ClockCache *clk = clock_cache_create(32);
    assert(clk != NULL);
    assert(clock_cache_put(clk, "x", "xv") == 0);
    assert(clock_cache_get(clk, "x", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "xv") == 0);

    for (int i = 0; i < 100; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "c%d", i);
        snprintf(v, sizeof(v), "cv%d", i);
        clock_cache_put(clk, k, v);
    }
    assert(clock_cache_hit_rate(clk) >= 0.0);
    clock_cache_destroy(clk);

    printf("test_cache: PASSED\n");
    return 0;
}
