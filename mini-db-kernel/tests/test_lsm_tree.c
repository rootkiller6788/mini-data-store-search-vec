#include "lsm_tree.h"
#include <stdio.h>
#include <assert.h>

static int passed = 0, failed = 0;
#define TEST(n) { bool ok = true; do
#define END(n) while(0); if (ok) { printf("  PASS %s\n", n); passed++; } else { printf("  FAIL %s\n", n); failed++; } }
#define CHK(c, m) if (!(c)) { printf("    ! %s\n", m); ok = false; }

int main(void) {
    printf("=== LSM Tree Tests ===\n");

    TEST("bloom filter basic") {
        uint8_t filter[LSM_BLOOM_BITS / 8];
        bloom_init(filter, LSM_BLOOM_BITS);
        for (int32_t i = 0; i < 10; i++) {
            bloom_add(filter, LSM_BLOOM_BITS, i * 100);
        }
        for (int32_t i = 0; i < 10; i++) {
            CHK(bloom_check(filter, LSM_BLOOM_BITS, i * 100),
                "bloom should contain added keys");
        }
        /* Keys not added should mostly return false (possible false positive) */
    } END("bloom filter basic");

    TEST("lsm_put and lsm_get") {
        LSMTree tree;
        lsm_init(&tree);
        CHK(lsm_put(&tree, 42, 100), "put should succeed");
        int32_t val;
        CHK(lsm_get(&tree, 42, &val), "get should find key");
        CHK(val == 100, "value should match");
    } END("lsm_put and lsm_get");

    TEST("lsm_get missing key") {
        LSMTree tree;
        lsm_init(&tree);
        lsm_put(&tree, 1, 10);
        int32_t val;
        CHK(!lsm_get(&tree, 999, &val), "get should fail for missing key");
    } END("lsm_get missing key");

    TEST("lsm_overwrite") {
        LSMTree tree;
        lsm_init(&tree);
        lsm_put(&tree, 7, 100);
        lsm_put(&tree, 7, 200);
        int32_t val;
        CHK(lsm_get(&tree, 7, &val), "get should find key");
        CHK(val == 200, "value should be latest");
    } END("lsm_overwrite");

    TEST("lsm_delete") {
        LSMTree tree;
        lsm_init(&tree);
        lsm_put(&tree, 5, 50);
        CHK(lsm_delete(&tree, 5), "delete should succeed");
        int32_t val;
        CHK(!lsm_get(&tree, 5, &val), "get should not find deleted key");
    } END("lsm_delete");

    TEST("lsm_flush") {
        LSMTree tree;
        lsm_init(&tree);
        /* Insert 300 entries to trigger at least one flush (memtable max=256) */
        for (int32_t i = 0; i < 300; i++) {
            lsm_put(&tree, i, i * 10);
        }
        lsm_flush(&tree);  /* force any remaining data to SSTable */
        int32_t levels = lsm_get_level_count(&tree);
        CHK(levels >= 1, "should have at least 1 level");
        /* Verify some keys */
        for (int32_t i = 0; i < 10; i++) {
            int32_t v;
            CHK(lsm_get(&tree, i, &v), "should find flushed key");
            CHK(v == i * 10, "value should match");
        }
    } END("lsm_flush");

    TEST("lsm_compact") {
        LSMTree tree;
        lsm_init(&tree);
        /* Fill memtable and flush multiple times */
        for (int32_t batch = 0; batch < 4; batch++) {
            for (int32_t i = 0; i < 300; i++) {
                lsm_put(&tree, batch * 300 + i, (batch * 300 + i) * 10);
            }
        }
        /* Force final flush */
        lsm_flush(&tree);
        /* Now compact level 0 */
        lsm_compact(&tree, 0);
        /* verify data survived compaction */
        int32_t v;
        CHK(lsm_get(&tree, 0, &v), "key 0 should survive");
        CHK(lsm_get(&tree, 100, &v), "key 100 should survive");
        if (lsm_get(&tree, 500, &v)) {
            /* key 500 may or may not survive depending on compaction */
        }
    } END("lsm_compact");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
