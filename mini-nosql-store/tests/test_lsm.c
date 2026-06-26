#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "lsm_engine.h"

int main(void) {
    LSMEngine *eng = lsm_create("./test_lsm_data");
    assert(eng != NULL);

    /* Put and get from memtable */
    assert(lsm_put(eng, "alpha", "value_a") == 0);
    assert(lsm_put(eng, "beta", "value_b") == 0);
    assert(lsm_put(eng, "gamma", "value_c") == 0);

    char buf[256];
    assert(lsm_get(eng, "alpha", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "value_a") == 0);
    assert(lsm_get(eng, "notfound", buf, sizeof(buf)) == -2);

    /* Flush */
    assert(lsm_flush_memtable(eng) == 0);
    assert(eng->level0_count >= 1);

    /* Get after flush */
    assert(lsm_get(eng, "beta", buf, sizeof(buf)) == 0);

    /* Tombstone delete */
    lsm_delete(eng, "gamma");
    assert(lsm_get(eng, "gamma", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "__TOMBSTONE__") == 0);

    /* Bloom filter test */
    BloomFilter bf;
    bloom_init(&bf);
    bloom_add(&bf, "hello");
    bloom_add(&bf, "world");
    assert(bloom_check(&bf, "hello") == 1);
    assert(bloom_check(&bf, "world") == 1);
    assert(bloom_check(&bf, "nonexist") == 0);

    /* Compaction */
    lsm_compact_all(eng);

    lsm_destroy(eng);
    printf("test_lsm: PASSED\n");
    return 0;
}
