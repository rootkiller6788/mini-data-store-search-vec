#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nosql_cache.h"

int main(void) {
    printf("=== Cache Engine Demo (LRU vs LFU vs Clock) ===\n\n");

    /* LRU Demo */
    printf("--- LRU Cache (capacity=5) ---\n");
    LRUCache *lru = lru_cache_create(5);
    lru_cache_put(lru, "a", "A", 0);
    lru_cache_put(lru, "b", "B", 0);
    lru_cache_put(lru, "c", "C", 0);
    lru_cache_put(lru, "d", "D", 0);
    lru_cache_put(lru, "e", "E", 0);
    printf("After 5 puts, size=%d\n", lru_cache_size(lru));

    /* Access 'a' to make it MRU, then add new to evict LRU ('b') */
    char buf[256];
    lru_cache_get(lru, "a", buf, sizeof(buf));
    lru_cache_put(lru, "f", "F", 0);
    printf("After accessing 'a' and adding 'f', size=%d\n", lru_cache_size(lru));

    int rc = lru_cache_get(lru, "b", buf, sizeof(buf));
    printf("'b' after eviction: %s\n", rc == -2 ? "EVICTED (LRU)" : "STILL PRESENT");
    rc = lru_cache_get(lru, "a", buf, sizeof(buf));
    printf("'a' after promote: %s\n", rc == 0 ? "PRESENT (promoted to MRU)" : "NOT FOUND");

    double hr_lru = lru_cache_hit_rate(lru);
    printf("LRU hit rate: %.2f\n\n", hr_lru);
    lru_cache_destroy(lru);

    /* LFU Demo */
    printf("--- LFU Cache (capacity=5) ---\n");
    LFUCache *lfu = lfu_cache_create(5);
    lfu_cache_put(lfu, "hot", "HOT");
    lfu_cache_put(lfu, "warm", "WARM");
    lfu_cache_put(lfu, "cold", "COLD");

    /* Access 'hot' 3 times */
    lfu_cache_get(lfu, "hot", buf, sizeof(buf));
    lfu_cache_get(lfu, "hot", buf, sizeof(buf));
    lfu_cache_get(lfu, "hot", buf, sizeof(buf));

    /* Access 'warm' once */
    lfu_cache_get(lfu, "warm", buf, sizeof(buf));

    /* Fill cache to trigger eviction of 'cold' (freq=1, LRU among them) */
    lfu_cache_put(lfu, "x1", "X1");
    lfu_cache_put(lfu, "x2", "X2");
    lfu_cache_put(lfu, "x3", "X3");
    lfu_cache_put(lfu, "x4", "X4");

    double hr_lfu = lfu_cache_hit_rate(lfu);
    printf("LFU hit rate: %.2f (hot+warm should survive eviction)\n", hr_lfu);
    lfu_cache_destroy(lfu);

    /* Clock Demo */
    printf("\n--- Clock Cache (capacity=5) ---\n");
    ClockCache *clk = clock_cache_create(5);
    clock_cache_put(clk, "p1", "page1");
    clock_cache_put(clk, "p2", "page2");
    clock_cache_put(clk, "p3", "page3");
    clock_cache_put(clk, "p4", "page4");
    clock_cache_put(clk, "p5", "page5");

    /* Access p1-p3 to set reference bits */
    clock_cache_get(clk, "p1", buf, sizeof(buf));
    clock_cache_get(clk, "p2", buf, sizeof(buf));
    clock_cache_get(clk, "p3", buf, sizeof(buf));

    /* Add new page — evicts first unreferenced page (p4 or p5) */
    clock_cache_put(clk, "p6", "page6");
    double hr_clk = clock_cache_hit_rate(clk);
    printf("Clock hit rate: %.2f\n", hr_clk);
    clock_cache_destroy(clk);

    printf("\nCache demo complete.\n");
    return 0;
}
