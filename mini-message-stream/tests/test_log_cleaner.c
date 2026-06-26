#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "log_cleaner.h"

static int test_config(void) {
    LogCleanerConfig cfg;
    assert(log_cleaner_config_init(&cfg, RETENTION_TIME, 86400000, -1) == 0);
    assert(cfg.policy == RETENTION_TIME);
    assert(cfg.retention_ms == 86400000);
    assert(log_cleaner_config_validate(&cfg) == 0);

    /* Invalid config */
    LogCleanerConfig bad_cfg;
    memset(&bad_cfg, 0, sizeof(bad_cfg));
    bad_cfg.min_cleanable_ratio = 2.0;
    assert(log_cleaner_config_validate(&bad_cfg) == -1);

    /* NULL safety */
    assert(log_cleaner_config_validate(NULL) == -1);
    return 0;
}

static int test_time_retention(void) {
    Topic *t = topic_create("time-topic", 1, 0);
    Partition *p = &t->partitions[0];
    assert(p != NULL);

    /* Fill first segment then roll */
    int i;
    for (i = 0; i < 100; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "k%d", i);
        snprintf(v, sizeof(v), "v%d", i);
        partition_append(p, k, v, "");
    }
    partition_roll_segment(p);

    /* Add more to new segment */
    for (i = 0; i < 50; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "nk%d", i);
        snprintf(v, sizeof(v), "nv%d", i);
        partition_append(p, k, v, "");
    }

    LogCleanerConfig cfg;
    log_cleaner_config_init(&cfg, RETENTION_TIME, 1, -1);  /* 1ms retention */
    CleanerStats stats;
    log_cleaner_stats_reset(&stats);

    int64_t far_future = 9999999999000LL;
    int deleted = log_cleaner_delete_expired_segments(p, &cfg, far_future, &stats);
    assert(deleted >= 0);

    log_cleaner_stats_print(&stats);
    topic_destroy(t);
    return 0;
}

static int test_compaction_map(void) {
    Topic *t = topic_create("comp-topic", 1, 0);
    Partition *p = &t->partitions[0];

    /* Write records with overlapping keys */
    partition_append(p, "key-a", "value-a1", "");
    partition_append(p, "key-b", "value-b1", "");
    partition_append(p, "key-a", "value-a2", "");  /* duplicate key */
    partition_append(p, "key-c", "value-c1", "");

    LogCompactionMap map;
    memset(&map, 0, sizeof(map));
    assert(log_compact_build_map(p, &map) == 0);

    /* Should have 3 unique keys (a, b, c) */
    assert(map.entry_count == 3);

    /* key-a should have offset 2 (latest) */
    int found_a = 0;
    int i;
    for (i = 0; i < map.entry_count; i++) {
        if (strcmp(map.entries[i].key, "key-a") == 0) {
            assert(map.entries[i].offset == 2);  /* latest */
            found_a = 1;
        }
    }
    assert(found_a == 1);

    topic_destroy(t);
    return 0;
}

static int test_compaction_apply(void) {
    Topic *t = topic_create("apply-topic", 1, 0);
    Partition *p = &t->partitions[0];

    partition_append(p, "k1", "v1-old", "");
    partition_append(p, "k2", "v2", "");
    partition_append(p, "k1", "v1-new", "");

    /* Roll so segment is inactive and can be compacted */
    partition_roll_segment(p);

    LogCompactionMap map;
    memset(&map, 0, sizeof(map));
    log_compact_build_map(p, &map);

    CleanerStats stats;
    log_cleaner_stats_reset(&stats);

    int removed = log_compact_apply_map(p, &map, &stats);
    assert(removed >= 1);

    log_cleaner_stats_print(&stats);
    topic_destroy(t);
    return 0;
}

static int test_tombstones(void) {
    Topic *t = topic_create("tomb-topic", 1, 0);
    Partition *p = &t->partitions[0];

    /* Create tombstone (empty value, non-empty key) */
    partition_append(p, "user-1", "", "");
    partition_append(p, "user-2", "data", "");

    int count = log_cleaner_count_tombstones(p);
    assert(count == 1);

    topic_destroy(t);
    return 0;
}

static int test_run_cycle(void) {
    Topic *t = topic_create("cycle-topic", 1, 0);
    Partition *p = &t->partitions[0];

    int i;
    for (i = 0; i < 50; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "k%d", i % 5);
        snprintf(v, sizeof(v), "v%d", i);
        partition_append(p, k, v, "");
    }

    LogCleanerConfig cfg;
    log_cleaner_config_init(&cfg, RETENTION_COMPACTION, 86400000, -1);
    CleanerStats stats;
    log_cleaner_stats_reset(&stats);

    int actions = log_cleaner_run_cycle(p, &cfg, 9999999999LL, &stats);
    assert(actions >= 0);
    log_cleaner_stats_print(&stats);

    topic_destroy(t);
    return 0;
}

static int test_dirty_ratio(void) {
    Topic *t = topic_create("dirty-topic", 1, 0);
    Partition *p = &t->partitions[0];

    partition_append(p, "key-x", "val-1", "");
    partition_append(p, "key-x", "val-2", "");
    partition_append(p, "key-x", "val-3", "");

    double ratio = log_cleaner_calculate_dirty_ratio(p);
    assert(ratio >= 0.0 && ratio <= 1.0);

    topic_destroy(t);
    return 0;
}

int main(void) {
    printf("=== Running Log Cleaner Tests ===\n");
    test_config();
    test_time_retention();
    test_compaction_map();
    test_compaction_apply();
    test_tombstones();
    test_run_cycle();
    test_dirty_ratio();
    printf("All log cleaner tests passed!\n");
    return 0;
}