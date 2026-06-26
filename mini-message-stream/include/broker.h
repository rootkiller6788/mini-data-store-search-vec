#ifndef BROKER_H
#define BROKER_H

#include <stdint.h>
#include "topic_partition.h"
#include "consumer_group.h"
#include "offset_manager.h"

#define MAX_BROKER_TOPICS     16
#define MAX_BROKER_PARTITIONS 64
#define MAX_BROKER_HOST_LEN   64

typedef struct {
    int         id;
    char        host[MAX_BROKER_HOST_LEN];
    int         port;
    Topic*      topics[MAX_BROKER_TOPICS];
    int         topic_count;
    Partition*  managed_partitions[MAX_BROKER_PARTITIONS];
    int         managed_count;
    int         is_controller;
    OffsetStore *offset_store;
} Broker;

Broker* broker_create(int id, const char *host, int port, int is_controller);
void    broker_destroy(Broker *b);
void    broker_start(Broker *b);

int    broker_handle_produce(Broker *b, const char *topic, int partition,
                             const char *key, const char *value,
                             const char *headers, int64_t *out_offset);

int    broker_handle_fetch(Broker *b, const char *topic, int partition,
                           int64_t fetch_offset, int max_bytes,
                           Record *out_records, int *out_count);

int    broker_handle_metadata(Broker *b, const char *topic,
                              int *out_partition_count,
                              int *out_leader_for_partition,
                              int *out_port);

int    broker_register_topic(Broker *b, Topic *topic);
int    broker_manage_partition(Broker *b, Partition *p);

#endif
