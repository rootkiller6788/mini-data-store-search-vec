#ifndef OFFSET_MANAGER_H
#define OFFSET_MANAGER_H

#include <stdint.h>

#define MAX_OFFSET_GROUPS  16
#define MAX_OFFSET_TOPICS  16
#define MAX_OFFSET_PARTS   8

typedef struct {
    int64_t committed_offset;
    int     is_set;
} PartitionOffset;

typedef struct {
    char             topic[128];
    PartitionOffset  partition_offsets[MAX_OFFSET_PARTS];
    int              partition_count;
} TopicOffset;

typedef struct {
    char         group_id[64];
    TopicOffset  topic_offsets[MAX_OFFSET_TOPICS];
    int          topic_count;
} GroupOffset;

typedef struct {
    GroupOffset groups[MAX_OFFSET_GROUPS];
    int         group_count;
} OffsetStore;

OffsetStore* offset_store_create(void);
void         offset_store_destroy(OffsetStore *os);

int          offset_commit(OffsetStore *os, const char *group_id,
                           const char *topic, int partition, int64_t offset);
int64_t      offset_fetch(OffsetStore *os, const char *group_id,
                          const char *topic, int partition);
int          offset_reset_to_earliest(OffsetStore *os, const char *group_id,
                                      const char *topic, int partition);
int          offset_reset_to_latest(OffsetStore *os, const char *group_id,
                                    const char *topic, int partition,
                                    int64_t latest_offset);

int64_t      consumer_lag_calculate(int64_t end_offset, int64_t committed_offset);
void         print_consumer_lag(const char *group_id, const char *topic,
                                int partition, int64_t end_offset,
                                int64_t committed_offset);

#endif
