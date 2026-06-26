#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "offset_manager.h"

OffsetStore* offset_store_create(void)
{
    OffsetStore *os;

    os = (OffsetStore*)calloc(1, sizeof(OffsetStore));
    if (!os) return NULL;

    os->group_count = 0;

    return os;
}

void offset_store_destroy(OffsetStore *os)
{
    free(os);
}

static int find_or_create_group(OffsetStore *os, const char *group_id)
{
    int i;

    for (i = 0; i < os->group_count; i++) {
        if (strcmp(os->groups[i].group_id, group_id) == 0) return i;
    }

    if (os->group_count >= MAX_OFFSET_GROUPS) return -1;

    i = os->group_count;
    strncpy(os->groups[i].group_id, group_id, 63);
    os->groups[i].group_id[63] = '\0';
    os->groups[i].topic_count = 0;
    os->group_count++;

    return i;
}

static int find_or_create_topic(GroupOffset *go, const char *topic)
{
    int t;

    for (t = 0; t < go->topic_count; t++) {
        if (strcmp(go->topic_offsets[t].topic, topic) == 0) return t;
    }

    if (go->topic_count >= MAX_OFFSET_TOPICS) return -1;

    t = go->topic_count;
    strncpy(go->topic_offsets[t].topic, topic, 127);
    go->topic_offsets[t].topic[127] = '\0';
    go->topic_offsets[t].partition_count = 0;
    go->topic_count++;

    return t;
}

int offset_commit(OffsetStore *os, const char *group_id,
                  const char *topic, int partition, int64_t offset)
{
    int gi, ti;

    if (!os || !group_id || !topic) return -1;

    gi = find_or_create_group(os, group_id);
    if (gi < 0) return -1;

    ti = find_or_create_topic(&os->groups[gi], topic);
    if (ti < 0) return -1;

    if (partition >= MAX_OFFSET_PARTS) return -1;

    os->groups[gi].topic_offsets[ti].partition_offsets[partition].committed_offset = offset;
    os->groups[gi].topic_offsets[ti].partition_offsets[partition].is_set = 1;

    if (partition >= os->groups[gi].topic_offsets[ti].partition_count) {
        os->groups[gi].topic_offsets[ti].partition_count = partition + 1;
    }

    return 0;
}

int64_t offset_fetch(OffsetStore *os, const char *group_id,
                     const char *topic, int partition)
{
    int gi, ti;

    if (!os || !group_id || !topic) return -1;

    gi = find_or_create_group(os, group_id);
    if (gi < 0) return -1;

    ti = find_or_create_topic(&os->groups[gi], topic);
    if (ti < 0) return -1;

    if (partition < MAX_OFFSET_PARTS &&
        os->groups[gi].topic_offsets[ti].partition_offsets[partition].is_set) {
        return os->groups[gi].topic_offsets[ti].partition_offsets[partition].committed_offset;
    }

    return 0;
}

int offset_reset_to_earliest(OffsetStore *os, const char *group_id,
                              const char *topic, int partition)
{
    return offset_commit(os, group_id, topic, partition, 0);
}

int offset_reset_to_latest(OffsetStore *os, const char *group_id,
                            const char *topic, int partition,
                            int64_t latest_offset)
{
    return offset_commit(os, group_id, topic, partition, latest_offset);
}

int64_t consumer_lag_calculate(int64_t end_offset, int64_t committed_offset)
{
    if (committed_offset < 0) return end_offset;
    if (committed_offset > end_offset) return 0;

    return end_offset - committed_offset;
}

void print_consumer_lag(const char *group_id, const char *topic,
                        int partition, int64_t end_offset,
                        int64_t committed_offset)
{
    int64_t lag;

    lag = consumer_lag_calculate(end_offset, committed_offset);

    printf("consumer-lag: group=%s, topic=%s, partition=%d\n", group_id, topic, partition);
    printf("  end_offset=%" PRId64 ", committed_offset=%" PRId64 ", lag=%" PRId64 "\n",
           end_offset, committed_offset, lag);

    if (lag > 100) {
        printf("  [WARN] high consumer lag — consumer may be falling behind\n");
    } else if (lag > 10) {
        printf("  [INFO] moderate lag\n");
    } else {
        printf("  [OK  ] lag is healthy\n");
    }
}
