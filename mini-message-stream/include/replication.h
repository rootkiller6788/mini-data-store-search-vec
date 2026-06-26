#ifndef REPLICATION_H
#define REPLICATION_H

#include <stdint.h>
#include "topic_partition.h"

#define MAX_ISR_SIZE          8
#define MAX_REPLICA_LAG_MS    10000

typedef enum {
    REPLICA_ROLE_LEADER    = 0,
    REPLICA_ROLE_FOLLOWER  = 1,
    REPLICA_ROLE_OBSERVER  = 2
} ReplicaRole;

typedef struct {
    int      broker_id;
    int64_t  log_end_offset;
    int64_t  last_fetch_ms;
    int      is_in_sync;
} ISREntry;

typedef struct {
    int      broker_id;
    int      epoch;
    ISREntry isr_list[MAX_ISR_SIZE];
    int      isr_count;
    int64_t  high_watermark;
    int64_t  last_commit_offset;
} ISRState;

typedef struct {
    int      follower_id;
    int      leader_id;
    int64_t  fetch_offset;
    int64_t  last_fetch_ms;
    int64_t  max_bytes;
} ReplicaFetcher;

typedef struct {
    int      partition_id;
    int      current_leader;
    int      current_epoch;
    int      candidates[MAX_ISR_SIZE];
    int      candidate_count;
    int      votes[MAX_ISR_SIZE];
    int      vote_count;
    int      voters[MAX_ISR_SIZE];
} LeaderElection;

ISRState*       isr_state_create(int broker_id, int epoch);
void            isr_state_destroy(ISRState *isr);

int             isr_add_replica(ISRState *isr, int broker_id, int64_t initial_leo);
int             isr_remove_replica(ISRState *isr, int broker_id);
int             isr_update_leo(ISRState *isr, int broker_id, int64_t new_leo,
                               int64_t fetch_time_ms);

int64_t         isr_advance_hw(ISRState *isr);
int64_t         isr_get_high_watermark(const ISRState *isr);
int             isr_count_in_sync(const ISRState *isr);

int             isr_maybe_shrink(ISRState *isr, int64_t now_ms, int64_t max_lag_ms);
int             isr_quorum_satisfied(const ISRState *isr, int64_t offset);

int             leader_election_init(LeaderElection *election, int partition_id,
                                     int current_epoch, const ISRState *isr);
int             leader_election_cast_vote(LeaderElection *election, int voter_id,
                                          int candidate_id);
int             leader_election_get_winner(const LeaderElection *election,
                                           int *out_leader_id);

ReplicaFetcher* replica_fetcher_create(int follower_id, int leader_id,
                                       int64_t start_offset);
void            replica_fetcher_destroy(ReplicaFetcher *rf);
int64_t         replica_fetcher_next_offset(ReplicaFetcher *rf);

int64_t         replica_calculate_truncation_offset(int64_t follower_leo,
                                                     int64_t leader_hw);

typedef struct {
    int min_insync_replicas;
    int replication_factor;
} ReplicationConfig;

int             replication_config_validate(ReplicationConfig *cfg);
const char*     replication_availability_status(const ISRState *isr,
                                                  const ReplicationConfig *cfg);

#endif