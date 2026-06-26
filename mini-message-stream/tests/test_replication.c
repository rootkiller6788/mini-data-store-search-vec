#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "replication.h"

static int test_isr_create(void) {
    ISRState *isr = isr_state_create(1, 0);
    assert(isr != NULL);
    assert(isr->broker_id == 1);
    assert(isr->epoch == 0);
    assert(isr->isr_count == 1);
    assert(isr->isr_list[0].broker_id == 1);
    assert(isr->high_watermark == 0);
    isr_state_destroy(isr);
    return 0;
}

static int test_isr_add_remove(void) {
    ISRState *isr = isr_state_create(1, 0);
    assert(isr != NULL);

    assert(isr_add_replica(isr, 2, 0) == 0);
    assert(isr->isr_count == 2);

    assert(isr_add_replica(isr, 3, 5) == 0);
    assert(isr->isr_count == 3);

    /* Duplicate add should update LEO */
    assert(isr_add_replica(isr, 2, 10) == 0);
    assert(isr->isr_count == 3);  /* Count unchanged */
    assert(isr->isr_list[1].log_end_offset == 10);

    /* Remove */
    assert(isr_remove_replica(isr, 2) == 0);
    assert(isr->isr_count == 2);

    /* Remove non-existent */
    assert(isr_remove_replica(isr, 99) == -1);

    isr_state_destroy(isr);
    return 0;
}

static int test_isr_hw_advance(void) {
    ISRState *isr = isr_state_create(1, 0);
    assert(isr != NULL);

    isr_add_replica(isr, 2, 0);
    isr_add_replica(isr, 3, 0);

    /* Update LEOs */
    isr_update_leo(isr, 1, 10, 100);
    isr_update_leo(isr, 2, 8, 100);
    isr_update_leo(isr, 3, 12, 100);

    /* HW = min(10, 8, 12) = 8 */
    assert(isr_advance_hw(isr) == 8);
    assert(isr_get_high_watermark(isr) == 8);

    /* Update lagging replica */
    isr_update_leo(isr, 2, 15, 200);
    /* HW = min(10, 15, 12) = 10 */
    assert(isr_advance_hw(isr) == 10);

    /* Count in-sync */
    assert(isr_count_in_sync(isr) == 3);

    isr_state_destroy(isr);
    return 0;
}

static int test_isr_shrink(void) {
    ISRState *isr = isr_state_create(1, 0);
    assert(isr != NULL);

    isr_add_replica(isr, 2, 0);
    isr_add_replica(isr, 3, 0);

    /* Broker 2 fetched at t=100, broker 3 fetched recently at t=15000 */
    isr_update_leo(isr, 2, 5, 100);
    isr_update_leo(isr, 3, 5, 15000);

    /* At t=20000, only broker 2 is lagging (19900ms > 10000ms max_lag) */
    assert(isr_maybe_shrink(isr, 20000, 10000) > 0);
    /* Broker 2 should be removed, broker 3 stays */
    assert(isr->isr_count == 2);  /* leader + broker 3 */

    /* Leader is never removed */
    isr_update_leo(isr, 1, 10, 15000);
    assert(isr_maybe_shrink(isr, 20000, 10000) == 0);

    isr_state_destroy(isr);
    return 0;
}

static int test_isr_quorum(void) {
    ISRState *isr = isr_state_create(1, 0);
    isr_add_replica(isr, 2, 0);

    isr_update_leo(isr, 1, 10, 100);
    isr_update_leo(isr, 2, 5, 100);

    /* Quorum at offset 3: both have LEO > 3? */
    assert(isr_quorum_satisfied(isr, 3) == 1);

    /* Quorum at offset 8: broker 2 hasn't caught up */
    assert(isr_quorum_satisfied(isr, 8) == 0);

    isr_state_destroy(isr);
    return 0;
}

static int test_leader_election(void) {
    ISRState *isr = isr_state_create(1, 0);
    isr_add_replica(isr, 2, 0);
    isr_add_replica(isr, 3, 0);

    LeaderElection election;
    assert(leader_election_init(&election, 0, 0, isr) == 0);
    assert(election.candidate_count == 3);

    /* All vote for candidate 2 */
    leader_election_cast_vote(&election, 1, 2);
    leader_election_cast_vote(&election, 2, 2);
    leader_election_cast_vote(&election, 3, 2);

    int winner;
    assert(leader_election_get_winner(&election, &winner) == 0);
    assert(winner == 2);

    /* Cannot vote twice */
    assert(leader_election_cast_vote(&election, 1, 3) == -1);

    isr_state_destroy(isr);
    return 0;
}

static int test_replica_fetcher(void) {
    ReplicaFetcher *rf = replica_fetcher_create(2, 1, 0);
    assert(rf != NULL);
    assert(rf->follower_id == 2);
    assert(rf->leader_id == 1);
    assert(replica_fetcher_next_offset(rf) == 0);
    replica_fetcher_destroy(rf);
    return 0;
}

static int test_truncation(void) {
    assert(replica_calculate_truncation_offset(10, 15) == 10);  /* no truncation */
    assert(replica_calculate_truncation_offset(15, 10) == 10);  /* truncate to 10 */
    assert(replica_calculate_truncation_offset(10, 10) == 10);  /* equal */
    return 0;
}

static int test_replication_config(void) {
    ReplicationConfig cfg = { 2, 3 };
    assert(replication_config_validate(&cfg) == 0);

    /* Invalid configs */
    cfg.replication_factor = 0;
    assert(replication_config_validate(&cfg) == -1);

    cfg.replication_factor = 3;
    cfg.min_insync_replicas = 4;  /* > factor */
    assert(replication_config_validate(&cfg) == -1);

    /* NULL safety */
    assert(replication_config_validate(NULL) == -1);

    /* Availability status */
    ISRState *isr = isr_state_create(1, 0);
    isr_add_replica(isr, 2, 0);
    isr_add_replica(isr, 3, 0);

    ReplicationConfig good_cfg = { 2, 3 };
    assert(strcmp(replication_availability_status(isr, &good_cfg), "HEALTHY") == 0);

    ReplicationConfig degraded_cfg = { 4, 5 };
    assert(strcmp(replication_availability_status(isr, &degraded_cfg), "UNDER_REPLICATED") == 0);

    isr_state_destroy(isr);
    return 0;
}

int main(void) {
    printf("=== Running Replication Tests ===\n");
    test_isr_create();
    test_isr_add_remove();
    test_isr_hw_advance();
    test_isr_shrink();
    test_isr_quorum();
    test_leader_election();
    test_replica_fetcher();
    test_truncation();
    test_replication_config();
    printf("All replication tests passed!\n");
    return 0;
}