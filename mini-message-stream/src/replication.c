#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "replication.h"

/* ================================================================
 * L4: ISR (In-Sync Replica) Management
 *
 * Theorem: High Watermark Monotonicity
 *   HW never decreases. HW = min(LEO_i for all i in ISR).
 *   This guarantees that any message below HW is durably stored
 *   on all ISR members, enabling consistent reads.
 *
 * Reference: Kafka Replication Protocol (KIP-101, KIP-227)
 * Course: MIT 6.824 (Distributed Systems) - Raft log replication
 *         CMU 15-440 (Distributed Systems) - Chain replication
 * ================================================================ */

ISRState* isr_state_create(int broker_id, int epoch)
{
    ISRState *isr;
    isr = (ISRState*)calloc(1, sizeof(ISRState));
    if (!isr) return NULL;
    isr->broker_id = broker_id;
    isr->epoch = epoch;
    isr->isr_count = 0;
    isr->high_watermark = 0;
    isr->last_commit_offset = -1;
    isr_add_replica(isr, broker_id, 0);
    return isr;
}

void isr_state_destroy(ISRState *isr)
{
    free(isr);
}

/* ISR Expansion: add a replica that has caught up with the leader.
 * L5: Algorithm - scan for duplicates, append if not found.
 * O(n) time, O(1) space */
int isr_add_replica(ISRState *isr, int broker_id, int64_t initial_leo)
{
    int i;
    if (!isr) return -1;
    if (isr->isr_count >= MAX_ISR_SIZE) return -1;
    for (i = 0; i < isr->isr_count; i++) {
        if (isr->isr_list[i].broker_id == broker_id) {
            isr->isr_list[i].log_end_offset = initial_leo;
            return 0;
        }
    }
    i = isr->isr_count;
    isr->isr_list[i].broker_id = broker_id;
    isr->isr_list[i].log_end_offset = initial_leo;
    isr->isr_list[i].last_fetch_ms = 0;
    isr->isr_list[i].is_in_sync = (initial_leo >= isr->high_watermark) ? 1 : 0;
    isr->isr_count++;
    printf("isr(broker=%d,epoch=%d): replica %d joined ISR, LEO=%" PRId64
           ", isr_size=%d\n", isr->broker_id, isr->epoch, broker_id,
           initial_leo, isr->isr_count);
    return 0;
}

/* ISR Shrink: remove a lagging or failed replica.
 * L5: Algorithm - linear scan, memmove shift. O(n) time. */
int isr_remove_replica(ISRState *isr, int broker_id)
{
    int i, j;
    if (!isr) return -1;
    for (i = 0; i < isr->isr_count; i++) {
        if (isr->isr_list[i].broker_id == broker_id) {
            printf("isr(broker=%d,epoch=%d): removing replica %d (LEO=%" PRId64 ")\n",
                   isr->broker_id, isr->epoch, broker_id,
                   isr->isr_list[i].log_end_offset);
            for (j = i; j < isr->isr_count - 1; j++) {
                memcpy(&isr->isr_list[j], &isr->isr_list[j + 1], sizeof(ISREntry));
            }
            isr->isr_count--;
            return 0;
        }
    }
    return -1;
}

/* L5: Update LEO for an ISR member.
 * When a follower fetches from leader, it reports its current LEO.
 * Leader uses this to track ISR health and advance HW.
 * O(n) time - must find the ISR entry. */
int isr_update_leo(ISRState *isr, int broker_id, int64_t new_leo,
                    int64_t fetch_time_ms)
{
    int i;
    if (!isr || new_leo < 0) return -1;
    for (i = 0; i < isr->isr_count; i++) {
        if (isr->isr_list[i].broker_id == broker_id) {
            if (new_leo > isr->isr_list[i].log_end_offset) {
                isr->isr_list[i].log_end_offset = new_leo;
            }
            isr->isr_list[i].last_fetch_ms = fetch_time_ms;
            isr->isr_list[i].is_in_sync = 1;
            return 0;
        }
    }
    return isr_add_replica(isr, broker_id, new_leo);
}

/* L4: High Watermark Advancement
 *
 * HW = min(LEO_i for all i in ISR)
 *
 * Key property: HW <= LEO_leader always.
 * Consumers can only read up to HW.
 * When all ISR members have copied message at offset X,
 * HW advances to X+1 (or more if contiguous).
 *
 * O(n) time where n = isr_count
 */
int64_t isr_advance_hw(ISRState *isr)
{
    int64_t min_leo;
    int i;
    if (!isr || isr->isr_count == 0) return -1;
    min_leo = isr->isr_list[0].log_end_offset;
    for (i = 1; i < isr->isr_count; i++) {
        if (isr->isr_list[i].log_end_offset < min_leo) {
            min_leo = isr->isr_list[i].log_end_offset;
        }
    }
    if (min_leo > isr->high_watermark) {
        int64_t old_hw = isr->high_watermark;
        isr->high_watermark = min_leo;
        printf("isr(broker=%d,epoch=%d): HW advanced %" PRId64 " -> %" PRId64 "\n",
               isr->broker_id, isr->epoch, old_hw, isr->high_watermark);
    }
    return isr->high_watermark;
}

int64_t isr_get_high_watermark(const ISRState *isr)
{
    return isr ? isr->high_watermark : -1;
}

/* Count ISR members that are currently in-sync.
 * L7: Application - used by Kafka's under-replicated-partitions metric. */
int isr_count_in_sync(const ISRState *isr)
{
    int count, i;
    if (!isr) return 0;
    count = 0;
    for (i = 0; i < isr->isr_count; i++) {
        if (isr->isr_list[i].is_in_sync) count++;
    }
    return count;
}

/* L5: ISR shrink by time-based lag detection.
 *
 * replica.lag.time.max.ms: if a follower hasn't fetched within this
 * window, it is kicked from ISR. This prevents a stalled follower
 * from blocking HW advancement and thus consumer progress.
 *
 * L4: CAP Theorem in action - this is an availability optimization.
 * By shrinking ISR, the system remains available for writes even
 * when some replicas are slow/failed, at the cost of durability.
 *
 * O(n) time */
int isr_maybe_shrink(ISRState *isr, int64_t now_ms, int64_t max_lag_ms)
{
    int i, removed;
    if (!isr) return 0;
    removed = 0;
    for (i = isr->isr_count - 1; i >= 0; i--) {
        ISREntry *entry = &isr->isr_list[i];
        if (entry->broker_id == isr->broker_id) continue;
        if (entry->last_fetch_ms > 0 &&
            (now_ms - entry->last_fetch_ms) > max_lag_ms) {
            printf("isr(broker=%d): replica %d lagged %" PRId64 "ms > %" PRId64
                   "ms, removing from ISR\n", isr->broker_id, entry->broker_id,
                   now_ms - entry->last_fetch_ms, max_lag_ms);
            isr_remove_replica(isr, entry->broker_id);
            removed++;
        }
    }
    return removed;
}

/* L5: Check if all ISR members have acknowledged up to 'offset'.
 *
 * For ProducerAck::ACKS_ALL, the producer waits until every ISR
 * member has replicated the message. This function checks whether
 * the quorum condition is met.
 *
 * Returns 1 if all ISR LEOs > offset, 0 otherwise.
 * O(n) time */
int isr_quorum_satisfied(const ISRState *isr, int64_t offset)
{
    int i;
    if (!isr || isr->isr_count == 0) return 0;
    for (i = 0; i < isr->isr_count; i++) {
        if (isr->isr_list[i].log_end_offset <= offset) return 0;
    }
    return 1;
}

/* ================================================================
 * L5: Leader Election Algorithm
 *
 * Based on ZAB (ZooKeeper Atomic Broadcast) / Kafka Controller logic.
 *
 * Selection rule:
 *   1. Only ISR members are eligible (have the committed data)
 *   2. Candidate with most complete log (highest LEO) wins
 *   3. Tie: lowest broker_id wins (deterministic)
 *
 * Reference:
 * - ZAB: ZooKeeper Atomic Broadcast Protocol (Reed/Junqueira 2008)
 * - Kafka KIP-232: Leader election protocol
 * ================================================================ */

int leader_election_init(LeaderElection *election, int partition_id,
                          int current_epoch, const ISRState *isr)
{
    int i;
    if (!election || !isr) return -1;
    election->partition_id = partition_id;
    election->current_leader = isr->broker_id;
    election->current_epoch = current_epoch + 1;
    election->candidate_count = 0;
    election->vote_count = 0;
    for (i = 0; i < isr->isr_count && i < MAX_ISR_SIZE; i++) {
        if (isr->isr_list[i].is_in_sync) {
            election->candidates[election->candidate_count++] =
                isr->isr_list[i].broker_id;
        }
    }
    printf("election(partition=%d,epoch=%d): %d candidates\n",
           partition_id, election->current_epoch, election->candidate_count);
    return 0;
}

int leader_election_cast_vote(LeaderElection *election, int voter_id,
                               int candidate_id)
{
    int i, j;
    if (!election) return -1;
    for (j = 0; j < election->vote_count; j++) {
        if (election->voters[j] == voter_id) return -1;
    }
    for (i = 0; i < election->candidate_count; i++) {
        if (election->candidates[i] == candidate_id) {
            election->voters[election->vote_count] = voter_id;
            election->votes[election->vote_count] = candidate_id;
            election->vote_count++;
            return 0;
        }
    }
    return -1;
}

/* Determine election winner by vote count.
 * Tie-breaking: lowest broker_id wins. */
int leader_election_get_winner(const LeaderElection *election,
                                int *out_leader_id)
{
    int vote_counts[MAX_ISR_SIZE];
    int i, j, max_votes, winner_idx;
    if (!election || !out_leader_id || election->candidate_count == 0)
        return -1;
    for (i = 0; i < election->candidate_count; i++) vote_counts[i] = 0;
    for (j = 0; j < election->vote_count; j++) {
        for (i = 0; i < election->candidate_count; i++) {
            if (election->candidates[i] == election->votes[j]) {
                vote_counts[i]++;
                break;
            }
        }
    }
    max_votes = -1;
    winner_idx = 0;
    for (i = 0; i < election->candidate_count; i++) {
        if (vote_counts[i] > max_votes) {
            max_votes = vote_counts[i];
            winner_idx = i;
        } else if (vote_counts[i] == max_votes &&
                   election->candidates[i] < election->candidates[winner_idx]) {
            winner_idx = i;
        }
    }
    *out_leader_id = election->candidates[winner_idx];
    printf("election(partition=%d,epoch=%d): winner=broker-%d (%d votes)\n",
           election->partition_id, election->current_epoch,
           *out_leader_id, max_votes);
    return 0;
}

/* ================================================================
 * L8: Replica Fetcher - Follower-side replication loop
 *
 * Follower brokers maintain a ReplicaFetcher for each leader
 * partition. They repeatedly issue FetchRequest to the leader,
 * append received records to their local log, and report LEO back.
 *
 * Advanced because it involves:
 * - Asynchronous catch-up (follower may be far behind)
 * - Back-pressure via max_bytes
 * - Pipelined fetch (fetch offsets may be in-flight)
 * ================================================================ */

ReplicaFetcher* replica_fetcher_create(int follower_id, int leader_id,
                                        int64_t start_offset)
{
    ReplicaFetcher *rf;
    rf = (ReplicaFetcher*)calloc(1, sizeof(ReplicaFetcher));
    if (!rf) return NULL;
    rf->follower_id = follower_id;
    rf->leader_id = leader_id;
    rf->fetch_offset = start_offset;
    rf->last_fetch_ms = 0;
    rf->max_bytes = 1024 * 1024;
    printf("fetcher(follower=%d): fetching from leader=%d, start_offset=%"
           PRId64 "\n", follower_id, leader_id, start_offset);
    return rf;
}

void replica_fetcher_destroy(ReplicaFetcher *rf)
{
    free(rf);
}

int64_t replica_fetcher_next_offset(ReplicaFetcher *rf)
{
    if (!rf) return -1;
    return rf->fetch_offset;
}

/* L6: Log Truncation on Leader Change
 *
 * Problem: When a new leader is elected, followers may have data
 * that the old leader wrote but was never committed (not in all ISR).
 * This uncommitted data must be truncated to the new leader's HW
 * to ensure log consistency.
 *
 * This is the log divergence problem in replicated state machines.
 * Raft solves it with log matching property; Kafka solves it with
 * HW-based truncation.
 */
int64_t replica_calculate_truncation_offset(int64_t follower_leo,
                                             int64_t leader_hw)
{
    if (follower_leo <= leader_hw) return follower_leo;
    printf("replica: truncation needed - follower_leo=%" PRId64
           " > leader_hw=%" PRId64 ", truncating to %" PRId64 "\n",
           follower_leo, leader_hw, leader_hw);
    return leader_hw;
}

/* L2: Replication configuration validation.
 *
 * L4: CAP Theorem - min.insync.replicas determines the tradeoff.
 *   min_insync = replication_factor: CP (consistent, no partition tolerance)
 *   min_insync = 1: AP (available under partition, may lose consistency)
 *   Typical: replication_factor=3, min_insync=2 -> tolerates 1 failure
 */
int replication_config_validate(ReplicationConfig *cfg)
{
    if (!cfg) return -1;
    if (cfg->replication_factor < 1 || cfg->replication_factor > MAX_ISR_SIZE) {
        fprintf(stderr, "replication: invalid factor %d (max %d)\n",
                cfg->replication_factor, MAX_ISR_SIZE);
        return -1;
    }
    if (cfg->min_insync_replicas < 1 ||
        cfg->min_insync_replicas > cfg->replication_factor) {
        fprintf(stderr, "replication: invalid min.insync %d (1..%d)\n",
                cfg->min_insync_replicas, cfg->replication_factor);
        return -1;
    }
    return 0;
}

/* L7: Production monitoring - partition health status.
 *
 * Used by Kafka's kafka-topics.sh --describe to report
 * under-replicated partitions. An URP means data is at risk.
 */
const char* replication_availability_status(const ISRState *isr,
                                              const ReplicationConfig *cfg)
{
    int in_sync;
    if (!isr || !cfg) return "UNKNOWN";
    in_sync = isr_count_in_sync(isr);
    if (in_sync >= cfg->replication_factor) return "HEALTHY";
    if (in_sync >= cfg->min_insync_replicas) return "DEGRADED";
    return "UNDER_REPLICATED";
}