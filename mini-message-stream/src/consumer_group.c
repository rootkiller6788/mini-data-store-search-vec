#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>
#include "consumer_group.h"

ConsumerGroup* group_create(const char *group_id, const char *coordinator,
                            PartitionStrategy strategy)
{
    ConsumerGroup *g;

    g = (ConsumerGroup*)calloc(1, sizeof(ConsumerGroup));
    if (!g) return NULL;

    strncpy(g->group_id, group_id, MAX_GROUP_ID_LEN - 1);
    g->group_id[MAX_GROUP_ID_LEN - 1] = '\0';

    strncpy(g->coordinator, coordinator, 127);
    g->coordinator[127] = '\0';

    g->strategy       = strategy;
    g->member_count   = 0;
    g->generation_id  = 0;
    g->is_rebalancing = 0;

    return g;
}

void group_destroy(ConsumerGroup *g)
{
    free(g);
}

int group_join(ConsumerGroup *g, const char *member_id,
               int session_timeout_ms)
{
    int i;

    if (!g || !member_id) return -1;

    for (i = 0; i < g->member_count; i++) {
        if (strcmp(g->members[i].member_id, member_id) == 0) {
            g->members[i].last_heartbeat = (int64_t)time(NULL);
            g->members[i].is_active = 1;
            return 0;
        }
    }

    if (g->member_count >= MAX_GROUP_MEMBERS) return -1;

    i = g->member_count;
    strncpy(g->members[i].member_id, member_id, MAX_MEMBER_ID_LEN - 1);
    g->members[i].member_id[MAX_MEMBER_ID_LEN - 1] = '\0';
    g->members[i].assigned_count     = 0;
    g->members[i].last_heartbeat     = (int64_t)time(NULL);
    g->members[i].session_timeout_ms = session_timeout_ms;
    g->members[i].is_active          = 1;
    g->member_count++;

    g->is_rebalancing = 1;
    g->generation_id++;

    printf("group(%s): member %s joined, generation=%d, triggering rebalance\n",
           g->group_id, member_id, g->generation_id);

    return 0;
}

void group_leave(ConsumerGroup *g, const char *member_id)
{
    int i, j;

    if (!g || !member_id) return;

    for (i = 0; i < g->member_count; i++) {
        if (strcmp(g->members[i].member_id, member_id) == 0) {
            g->members[i].is_active = 0;
            for (j = i; j < g->member_count - 1; j++) {
                memcpy(&g->members[j], &g->members[j + 1],
                       sizeof(ConsumerMember));
            }
            g->member_count--;
            g->is_rebalancing = 1;
            printf("group(%s): member %s left, triggering rebalance\n",
                   g->group_id, member_id);
            return;
        }
    }
}

static int group_assign_range(ConsumerGroup *g, Topic *topics[], int topic_count)
{
    int t, p, m, total_partitions, parts_per_member, extra, start, end;

    if (!g->member_count) return -1;

    total_partitions = 0;
    for (t = 0; t < topic_count; t++) {
        total_partitions += topics[t]->partition_count;
    }

    parts_per_member = total_partitions / g->member_count;
    extra = total_partitions % g->member_count;
    start = 0;

    for (m = 0; m < g->member_count; m++) {
        int assigned = parts_per_member + (m < extra ? 1 : 0);
        end = start + assigned;
        if (end > total_partitions) end = total_partitions;

        g->members[m].assigned_count = 0;
        for (p = start; p < end && g->members[m].assigned_count < MAX_PARTITIONS; p++) {
            int part_id = p;
            g->members[m].assigned_partitions[g->members[m].assigned_count++] = part_id;
        }
        start = end;
    }

    return 0;
}

static int group_assign_round_robin(ConsumerGroup *g, Topic *topics[], int topic_count)
{
    int m, t, p, part_idx;

    if (!g->member_count) return -1;

    for (m = 0; m < g->member_count; m++) {
        g->members[m].assigned_count = 0;
    }

    part_idx = 0;
    for (t = 0; t < topic_count; t++) {
        for (p = 0; p < topics[t]->partition_count; p++) {
            m = part_idx % g->member_count;
            if (g->members[m].assigned_count < MAX_PARTITIONS) {
                g->members[m].assigned_partitions[
                    g->members[m].assigned_count++] = p;
            }
            part_idx++;
        }
    }

    return 0;
}

int group_sync(ConsumerGroup *g, Topic *topics[], int topic_count)
{
    int m, p;

    if (!g || !topics || topic_count < 1) return -1;

    printf("group(%s): syncing — rebalance, generation %d, members %d, strategy %s\n",
           g->group_id, g->generation_id, g->member_count,
           g->strategy == STRATEGY_RANGE ? "RANGE" : "ROUND_ROBIN");

    if (g->strategy == STRATEGY_RANGE) {
        group_assign_range(g, topics, topic_count);
    } else {
        group_assign_round_robin(g, topics, topic_count);
    }

    for (m = 0; m < g->member_count; m++) {
        ConsumerMember *member = &g->members[m];
        printf("  member(%s) assigned [", member->member_id);
        for (p = 0; p < member->assigned_count; p++) {
            printf("%s%d", p > 0 ? "," : "", member->assigned_partitions[p]);
        }
        printf("]\n");
    }

    g->is_rebalancing = 0;
    return 0;
}

int group_heartbeat(ConsumerGroup *g, const char *member_id)
{
    int i;

    if (!g || !member_id) return -1;

    for (i = 0; i < g->member_count; i++) {
        if (strcmp(g->members[i].member_id, member_id) == 0) {
            int64_t now = (int64_t)time(NULL);
            int64_t elapsed = now - g->members[i].last_heartbeat;
            g->members[i].last_heartbeat = now;

            if (elapsed > g->members[i].session_timeout_ms / 1000) {
                printf("group(%s): member %s session timed out (%" PRId64 "s)\n",
                       g->group_id, member_id, elapsed);
                g->members[i].is_active = 0;
                g->is_rebalancing = 1;
                return -1;
            }
            return 0;
        }
    }

    return -1;
}

int group_get_assignment(const ConsumerGroup *g, const char *member_id,
                         int *out_partitions, int *out_count)
{
    int i, p;

    if (!g || !member_id || !out_partitions || !out_count) return -1;

    for (i = 0; i < g->member_count; i++) {
        if (strcmp(g->members[i].member_id, member_id) == 0) {
            *out_count = g->members[i].assigned_count;
            for (p = 0; p < g->members[i].assigned_count; p++) {
                out_partitions[p] = g->members[i].assigned_partitions[p];
            }
            return 0;
        }
    }

    *out_count = 0;
    return -1;
}

OffsetCommit* offset_commit_create(const char *topic, int partition,
                                   int64_t offset)
{
    OffsetCommit *oc = (OffsetCommit*)malloc(sizeof(OffsetCommit));
    if (!oc) return NULL;

    strncpy(oc->topic, topic, 127);
    oc->topic[127] = '\0';
    oc->partition = partition;
    oc->offset    = offset;

    return oc;
}

void offset_commit_destroy(OffsetCommit *oc)
{
    free(oc);
}
