#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "kv_store.h"

int main(void) {
    KVStore *s = kv_create(0);
    assert(s != NULL);

    assert(kv_put(s, "key1", "val1", 0) == 0);
    assert(kv_put(s, "key2", "val2", 0) == 0);
    assert(kv_count(s) == 2);

    char buf[256];
    assert(kv_get(s, "key1", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "val1") == 0);

    assert(kv_delete(s, "key1") == 0);
    assert(kv_get(s, "key1", buf, sizeof(buf)) == -2);
    assert(kv_count(s) == 1);

    /* TTL test */
    assert(kv_put(s, "temp", "expired", time(NULL) - 10) == 0);
    assert(kv_get(s, "temp", buf, sizeof(buf)) == -2);
    assert(kv_count(s) == 1);

    /* Prefix scan */
    assert(kv_put(s, "user:a", "alice", 0) == 0);
    assert(kv_put(s, "user:b", "bob", 0) == 0);
    KVPair *results[16];
    int n = kv_scan_prefix(s, "user:", results, 16);
    assert(n == 2);

    kv_destroy(s);
    printf("test_kv: PASSED\n");
    return 0;
}
