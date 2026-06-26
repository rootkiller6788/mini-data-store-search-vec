#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "consumer_group.h"

static int test_group_create(void) {
    ConsumerGroup *g = group_create("test-group", "localhost:9092",
                                     STRATEGY_RANGE);
    assert(g != NULL);
    assert(strcmp(g->group_id, "test-group") == 0);
    assert(g->strategy == STRATEGY_RANGE);
    assert(g->member_count == 0);
    assert(g->generation_id == 0);
    group_destroy(g);
    return 0;
}

static int test_group_join(void) {
    ConsumerGroup *g = group_create("g1", "c:9092", STRATEGY_RANGE);
    assert(g != NULL);

    assert(group_join(g, "member-A", 30000) == 0);
    assert(g->member_count == 1);
    assert(strcmp(g->members[0].member_id, "member-A") == 0);
    assert(g->is_rebalancing == 1);

    /* Join same member again - rejoin */
    assert(group_join(g, "member-A", 30000) == 0);
    assert(g->member_count == 1);

    /* Join second member */
    assert(group_join(g, "member-B", 30000) == 0);
    assert(g->member_count == 2);

    /* NULL safety */
    assert(group_join(NULL, "m", 1000) == -1);
    assert(group_join(g, NULL, 1000) == -1);

    group_destroy(g);
    return 0;
}

static int test_group_leave(void) {
    ConsumerGroup *g = group_create("g2", "c:9092", STRATEGY_RANGE);
    assert(g != NULL);

    group_join(g, "A", 30000);
    group_join(g, "B", 30000);
    group_join(g, "C", 30000);
    assert(g->member_count == 3);

    group_leave(g, "B");
    assert(g->member_count == 2);
    assert(g->is_rebalancing == 1);

    /* Leave non-existent member - no-op */
    group_leave(g, "X");
    assert(g->member_count == 2);

    /* NULL safety */
    group_leave(NULL, "A");
    group_leave(g, NULL);

    group_destroy(g);
    return 0;
}

static int test_group_sync_range(void) {
    ConsumerGroup *g = group_create("g3", "c:9092", STRATEGY_RANGE);
    Topic *t = topic_create("sync-topic", 6, 0);

    group_join(g, "A", 30000);
    group_join(g, "B", 30000);

    Topic *tlist[] = { t };
    assert(group_sync(g, tlist, 1) == 0);
    assert(g->is_rebalancing == 0);

    /* Each member should have assigned partitions */
    assert(g->members[0].assigned_count > 0);
    assert(g->members[1].assigned_count > 0);
    /* Total assigned = 6 */
    assert(g->members[0].assigned_count + g->members[1].assigned_count == 6);

    topic_destroy(t);
    group_destroy(g);
    return 0;
}

static int test_group_sync_round_robin(void) {
    ConsumerGroup *g = group_create("g4", "c:9092", STRATEGY_ROUND_ROBIN);
    Topic *t = topic_create("rr-topic", 4, 0);

    group_join(g, "A", 30000);
    group_join(g, "B", 30000);

    Topic *tlist[] = { t };
    assert(group_sync(g, tlist, 1) == 0);

    /* Both should have 2 partitions each */
    assert(g->members[0].assigned_count == 2);
    assert(g->members[1].assigned_count == 2);

    topic_destroy(t);
    group_destroy(g);
    return 0;
}

static int test_group_heartbeat(void) {
    ConsumerGroup *g = group_create("g5", "c:9092", STRATEGY_RANGE);
    group_join(g, "A", 1);  /* 1ms timeout */

    /* First heartbeat should be OK */
    assert(group_heartbeat(g, "A") == 0);

    /* NULL safety */
    assert(group_heartbeat(NULL, "A") == -1);
    assert(group_heartbeat(g, NULL) == -1);
    assert(group_heartbeat(g, "unknown") == -1);

    group_destroy(g);
    return 0;
}

static int test_group_assignment(void) {
    ConsumerGroup *g = group_create("g6", "c:9092", STRATEGY_RANGE);
    Topic *t = topic_create("assign-topic", 4, 0);
    int parts[16], count;

    group_join(g, "A", 30000);
    group_join(g, "B", 30000);
    Topic *tlist[] = { t };
    group_sync(g, tlist, 1);

    assert(group_get_assignment(g, "A", parts, &count) == 0);
    assert(count > 0);

    /* Unknown member */
    assert(group_get_assignment(g, "X", parts, &count) == -1);
    assert(count == 0);

    /* NULL safety */
    assert(group_get_assignment(NULL, "A", parts, &count) == -1);

    topic_destroy(t);
    group_destroy(g);
    return 0;
}

static int test_offset_commit_struct(void) {
    OffsetCommit *oc = offset_commit_create("t1", 0, 42);
    assert(oc != NULL);
    assert(strcmp(oc->topic, "t1") == 0);
    assert(oc->partition == 0);
    assert(oc->offset == 42);
    offset_commit_destroy(oc);
    offset_commit_destroy(NULL);
    return 0;
}

int main(void) {
    printf("=== Running Consumer Group Tests ===\n");
    test_group_create();
    test_group_join();
    test_group_leave();
    test_group_sync_range();
    test_group_sync_round_robin();
    test_group_heartbeat();
    test_group_assignment();
    test_offset_commit_struct();
    printf("All consumer group tests passed!\n");
    return 0;
}