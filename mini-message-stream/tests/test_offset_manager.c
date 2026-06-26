#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "offset_manager.h"

static int test_offset_store_create(void) {
    OffsetStore *os = offset_store_create();
    assert(os != NULL);
    assert(os->group_count == 0);
    offset_store_destroy(os);
    return 0;
}

static int test_offset_commit(void) {
    OffsetStore *os = offset_store_create();
    assert(os != NULL);

    assert(offset_commit(os, "group1", "topic1", 0, 100) == 0);
    assert(offset_commit(os, "group1", "topic1", 1, 200) == 0);
    assert(offset_commit(os, "group2", "topic1", 0, 300) == 0);

    assert(os->group_count == 2);

    /* NULL safety */
    assert(offset_commit(NULL, "g", "t", 0, 0) == -1);
    assert(offset_commit(os, NULL, "t", 0, 0) == -1);
    assert(offset_commit(os, "g", NULL, 0, 0) == -1);

    offset_store_destroy(os);
    return 0;
}

static int test_offset_fetch(void) {
    OffsetStore *os = offset_store_create();
    assert(os != NULL);

    offset_commit(os, "g1", "t1", 0, 42);
    assert(offset_fetch(os, "g1", "t1", 0) == 42);

    /* Uncommitted partition returns 0 */
    assert(offset_fetch(os, "g1", "t1", 1) == 0);

    /* Unknown group returns 0 */
    assert(offset_fetch(os, "unknown", "t1", 0) == 0);

    /* Out of range partition returns 0 (no offset committed) */
    assert(offset_fetch(os, "g1", "t1", MAX_OFFSET_PARTS) == 0);

    offset_store_destroy(os);
    return 0;
}

static int test_offset_reset(void) {
    OffsetStore *os = offset_store_create();
    assert(os != NULL);

    offset_commit(os, "g1", "t1", 0, 100);

    /* Reset to earliest */
    assert(offset_reset_to_earliest(os, "g1", "t1", 0) == 0);
    assert(offset_fetch(os, "g1", "t1", 0) == 0);

    /* Reset to latest */
    assert(offset_reset_to_latest(os, "g1", "t1", 0, 999) == 0);
    assert(offset_fetch(os, "g1", "t1", 0) == 999);

    offset_store_destroy(os);
    return 0;
}

static int test_consumer_lag(void) {
    /* Healthy lag */
    assert(consumer_lag_calculate(100, 90) == 10);

    /* No lag */
    assert(consumer_lag_calculate(100, 100) == 0);

    /* Consumer ahead (shouldn't happen, but handled) */
    assert(consumer_lag_calculate(100, 150) == 0);

    /* No committed offset */
    assert(consumer_lag_calculate(100, -1) == 100);

    /* Zero end offset */
    assert(consumer_lag_calculate(0, 0) == 0);

    /* Print lag (just exercises the code path) */
    print_consumer_lag("test-group", "test-topic", 0, 100, 90);
    print_consumer_lag("test-group", "test-topic", 0, 100, 0);    /* high lag */
    print_consumer_lag("test-group", "test-topic", 0, 100, 85);   /* moderate */
    print_consumer_lag("test-group", "test-topic", 0, 100, 99);   /* low */

    return 0;
}

int main(void) {
    printf("=== Running Offset Manager Tests ===\n");
    test_offset_store_create();
    test_offset_commit();
    test_offset_fetch();
    test_offset_reset();
    test_consumer_lag();
    printf("All offset manager tests passed!\n");
    return 0;
}