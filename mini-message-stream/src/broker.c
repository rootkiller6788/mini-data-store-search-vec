#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "broker.h"

Broker* broker_create(int id, const char *host, int port, int is_controller)
{
    Broker *b;

    b = (Broker*)calloc(1, sizeof(Broker));
    if (!b) return NULL;

    b->id           = id;
    strncpy(b->host, host, MAX_BROKER_HOST_LEN - 1);
    b->host[MAX_BROKER_HOST_LEN - 1] = '\0';
    b->port         = port;
    b->is_controller = is_controller;
    b->topic_count   = 0;
    b->managed_count = 0;
    b->offset_store  = offset_store_create();

    return b;
}

void broker_destroy(Broker *b)
{
    int i;
    if (!b) return;

    for (i = 0; i < b->topic_count; i++) {
        topic_destroy(b->topics[i]);
    }

    offset_store_destroy(b->offset_store);
    free(b);
}

void broker_start(Broker *b)
{
    if (!b) return;

    printf("broker(%d) started on %s:%d [%s]\n",
           b->id, b->host, b->port,
           b->is_controller ? "CONTROLLER" : "FOLLOWER");
    printf("  managed partitions: %d, topics: %d\n",
           b->managed_count, b->topic_count);
}

int broker_register_topic(Broker *b, Topic *topic)
{
    int i;

    if (!b || !topic) return -1;
    if (b->topic_count >= MAX_BROKER_TOPICS) return -1;

    b->topics[b->topic_count++] = topic;

    for (i = 0; i < topic->partition_count; i++) {
        if (b->managed_count < MAX_BROKER_PARTITIONS) {
            topic->partitions[i].leader_broker = b->id;
            b->managed_partitions[b->managed_count++] = &topic->partitions[i];
        }
    }

    printf("broker(%d): registered topic %s (%d partitions)\n",
           b->id, topic->name, topic->partition_count);

    return 0;
}

int broker_manage_partition(Broker *b, Partition *p)
{
    if (!b || !p) return -1;
    if (b->managed_count >= MAX_BROKER_PARTITIONS) return -1;

    p->leader_broker = b->id;
    b->managed_partitions[b->managed_count++] = p;

    return 0;
}

int broker_handle_produce(Broker *b, const char *topic, int partition,
                          const char *key, const char *value,
                          const char *headers, int64_t *out_offset)
{
    Topic *t = NULL;
    Partition *p;
    int i;
    int64_t offset;

    if (!b || !topic || !out_offset) return -1;

    for (i = 0; i < b->topic_count; i++) {
        if (strcmp(b->topics[i]->name, topic) == 0) {
            t = b->topics[i];
            break;
        }
    }

    if (!t) {
        printf("broker(%d): topic %s not found\n", b->id, topic);
        return -1;
    }

    if (partition < 0 || partition >= t->partition_count) return -1;

    p = &t->partitions[partition];

    if (p->leader_broker != b->id && !b->is_controller) {
        printf("broker(%d): not leader for topic=%s partition=%d (leader=%d)\n",
               b->id, topic, partition, p->leader_broker);
        return -1;
    }

    offset = partition_append(p, key, value, headers);
    if (offset < 0) return -1;

    *out_offset = offset;

    printf("broker(%d): PRODUCE topic=%s partition=%d offset=%" PRId64 "\n",
           b->id, topic, partition, offset);

    return 0;
}

int broker_handle_fetch(Broker *b, const char *topic, int partition,
                        int64_t fetch_offset, int max_bytes,
                        Record *out_records, int *out_count)
{
    Topic *t = NULL;
    Partition *p;
    int i, max_records;

    if (!b || !topic || !out_records || !out_count) return -1;

    for (i = 0; i < b->topic_count; i++) {
        if (strcmp(b->topics[i]->name, topic) == 0) {
            t = b->topics[i];
            break;
        }
    }

    if (!t) {
        printf("broker(%d): topic %s not found (fetch)\n", b->id, topic);
        *out_count = 0;
        return -1;
    }

    if (partition < 0 || partition >= t->partition_count) {
        *out_count = 0;
        return -1;
    }

    p = &t->partitions[partition];

    max_records = max_bytes / (int)sizeof(Record);
    if (max_records < 1) max_records = 1;

    return partition_read(p, fetch_offset, max_records, out_records, out_count);
}

int broker_handle_metadata(Broker *b, const char *topic,
                           int *out_partition_count,
                           int *out_leader_for_partition,
                           int *out_port)
{
    int i;

    if (!b || !topic || !out_partition_count || !out_leader_for_partition || !out_port)
        return -1;

    for (i = 0; i < b->topic_count; i++) {
        if (strcmp(b->topics[i]->name, topic) == 0) {
            *out_partition_count = b->topics[i]->partition_count;

            if (*out_partition_count > 0) {
                *out_leader_for_partition = b->topics[i]->partitions[0].leader_broker;
            } else {
                *out_leader_for_partition = -1;
            }

            *out_port = b->port;
            return 0;
        }
    }

    printf("broker(%d): metadata for topic %s not found\n", b->id, topic);
    *out_partition_count = 0;
    *out_leader_for_partition = -1;
    *out_port = -1;
    return -1;
}
