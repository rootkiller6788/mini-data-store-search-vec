#ifndef CONSUMER_GROUP_H
#define CONSUMER_GROUP_H

#include <stdint.h>
#include "topic_partition.h"

#define MAX_GROUP_MEMBERS     8
#define MAX_SUBSCRIBED_TOPICS 8
#define MAX_GROUP_ID_LEN      64
#define MAX_MEMBER_ID_LEN     64

typedef struct {
    char topic[128];
    int  partition;
    int64_t offset;
} OffsetCommit;

typedef struct {
    char       member_id[MAX_MEMBER_ID_LEN];
    int        assigned_partitions[MAX_PARTITIONS];
    int        assigned_count;
    int64_t    last_heartbeat;
    int        session_timeout_ms;
    int        is_active;
} ConsumerMember;

typedef enum {
    STRATEGY_RANGE,
    STRATEGY_ROUND_ROBIN
} PartitionStrategy;

typedef struct {
    char              group_id[MAX_GROUP_ID_LEN];
    ConsumerMember    members[MAX_GROUP_MEMBERS];
    int               member_count;
    char              subscribed_topics[MAX_SUBSCRIBED_TOPICS][128];
    int               subscribed_count;
    char              coordinator[128];
    int               generation_id;
    PartitionStrategy strategy;
    int               is_rebalancing;
} ConsumerGroup;

ConsumerGroup* group_create(const char *group_id, const char *coordinator,
                            PartitionStrategy strategy);
void           group_destroy(ConsumerGroup *g);
int            group_join(ConsumerGroup *g, const char *member_id,
                          int session_timeout_ms);
int            group_sync(ConsumerGroup *g, Topic *topics[], int topic_count);
int            group_heartbeat(ConsumerGroup *g, const char *member_id);
void           group_leave(ConsumerGroup *g, const char *member_id);
int            group_get_assignment(const ConsumerGroup *g,
                                    const char *member_id,
                                    int *out_partitions, int *out_count);
OffsetCommit*  offset_commit_create(const char *topic, int partition,
                                    int64_t offset);
void           offset_commit_destroy(OffsetCommit *oc);

#endif
