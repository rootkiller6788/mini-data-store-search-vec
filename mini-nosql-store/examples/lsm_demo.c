#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsm_engine.h"

int main(void) {
    printf("=== mini-nosql LSM-Tree Engine Demo ===\n\n");

    LSMEngine *engine = lsm_create("./lsm_data");
    if (!engine) {
        fprintf(stderr, "Failed to create LSM engine\n");
        return 1;
    }

    printf("Putting 1000 keys into memtable...\n");
    for (int i = 0; i < 1000; i++) {
        char key[64], value[256];
        snprintf(key, sizeof(key), "key:%06d", i);
        snprintf(value, sizeof(value), "value_data_%06d", i);
        lsm_put(engine, key, value);
    }
    printf("Memtable count: %d\n", engine->memtable->count);

    printf("\n--- Testing GET from memtable ---\n");
    char val[256];
    for (int i = 0; i < 5; i++) {
        char key[64];
        snprintf(key, sizeof(key), "key:%06d", i * 100);
        if (lsm_get(engine, key, val, sizeof(val)) == 0)
            printf("  GET %s -> %s\n", key, val);
    }

    printf("\n--- Flushing memtable to immutable + SSTable ---\n");
    int old_mem_count = engine->memtable->count;
    lsm_flush_memtable(engine);
    printf("Old memtable frozen (%d entries), new memtable created.\n", old_mem_count);
    printf("Immutable memtables: %d\n", engine->imm_count);
    printf("Level-0 SSTables: %d\n", engine->level0_count);

    printf("\n--- Testing GET after flush ---\n");
    for (int i = 0; i < 5; i++) {
        char key[64];
        snprintf(key, sizeof(key), "key:%06d", i * 100);
        int rc = lsm_get(engine, key, val, sizeof(val));
        if (rc == 0)
            printf("  GET %s -> %s (found in Level-0)\n", key, val);
        else
            printf("  GET %s -> NOT FOUND (%d)\n", key, rc);
    }

    printf("\n--- Adding more keys to new memtable ---\n");
    for (int i = 1000; i < 1100; i++) {
        char key[64], value[256];
        snprintf(key, sizeof(key), "key:%06d", i);
        snprintf(value, sizeof(value), "extra_value_%06d", i);
        lsm_put(engine, key, value);
    }
    printf("New memtable count: %d\n", engine->memtable->count);

    printf("\n--- Flushing again... ---\n");
    lsm_flush_memtable(engine);
    printf("Level-0 SSTables now: %d\n", engine->level0_count);

    printf("\n--- Running compaction (Level-0 -> Level-1) ---\n");
    lsm_compact_all(engine);
    printf("Level-0 SSTables: %d\n", engine->level0_count);
    printf("Level-1 SSTables: %d\n", engine->level1_count);
    printf("Total key count (estimated): %d\n", lsm_count(engine));

    printf("\n--- Test DELETE with tombstone ---\n");
    lsm_delete(engine, "key:000000");
    lsm_delete(engine, "key:000001");
    int rc = lsm_get(engine, "key:000000", val, sizeof(val));
    printf("GET key:000000 (deleted) -> %s\n", rc == 0 ? val : "NOT FOUND (tombstone)");

    rc = lsm_get(engine, "key:000002", val, sizeof(val));
    printf("GET key:000002 -> %s\n", rc == 0 ? val : "NOT FOUND");

    printf("\n--- Bloom filter tests ---\n");
    BloomFilter bf;
    bloom_init(&bf);
    bloom_add(&bf, "test-key-1");
    bloom_add(&bf, "test-key-2");
    bloom_add(&bf, "test-key-3");
    printf("bloom_check('test-key-1'): %s\n",
           bloom_check(&bf, "test-key-1") ? "MAYBE PRESENT" : "DEFINITELY NOT");
    printf("bloom_check('test-key-99'): %s\n",
           bloom_check(&bf, "test-key-99") ? "MAYBE PRESENT" : "DEFINITELY NOT");

    lsm_destroy(engine);
    printf("\nLSM engine demo complete.\n");
    return 0;
}
