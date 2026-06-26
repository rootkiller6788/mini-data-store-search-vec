#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "topic_partition.h"
#include "producer_client.h"
#include "broker.h"
#include "offset_manager.h"

int main(void)
{
    Broker      *broker;
    Topic       *topic;
    OffsetStore *os;
    Record       records[16];
    int          i, count, part_count, leader, port;
    int64_t      offset;

    printf("=== Single Broker Demo ===\n\n");

    broker = broker_create(0, "0.0.0.0", 9092, 1);
    broker_start(broker);

    topic = topic_create("events", 3, 604800000);
    broker_register_topic(broker, topic);

    os = offset_store_create();

    printf("\n--- Metadata Request ---\n");
    if (broker_handle_metadata(broker, "events", &part_count, &leader, &port) == 0) {
        printf("metadata: topic=events, partitions=%d, leader=%d, port=%d\n",
               part_count, leader, port);
    }

    printf("\n--- Produce 30 Records ---\n");
    for (i = 0; i < 30; i++) {
        char key[32], value[96];
        snprintf(key, sizeof(key), "device-%02d", i % 4);
        snprintf(value, sizeof(value),
                 "{\"event\":\"sensor_read\",\"device\":\"%s\",\"value\":%d}",
                 key, i * 10);

        int part = i % topic->partition_count;
        broker_handle_produce(broker, "events", part, key, value, "", &offset);
        printf("  [%02d] partition=%d offset=%" PRId64 " key=%s\n",
               i, part, offset, key);
    }

    printf("\n--- Fetch Records (round 1) ---\n");
    for (i = 0; i < topic->partition_count; i++) {
        count = 0;
        broker_handle_fetch(broker, "events", i, 0, 4096, records, &count);
        printf("  partition %d: fetched %d records\n", i, count);

        if (count > 0) {
            offset_commit(os, "demo-group", "events", i,
                          records[count - 1].offset + 1);
        }
    }

    printf("\n--- Consumer Lag Report ---\n");
    for (i = 0; i < topic->partition_count; i++) {
        int64_t committed = offset_fetch(os, "demo-group", "events", i);
        int64_t end_off   = partition_get_end_offset(&topic->partitions[i]);
        print_consumer_lag("demo-group", "events", i, end_off, committed);
    }

    printf("\n--- Fetch Records (round 2 - incremental) ---\n");
    for (i = 0; i < 10; i++) {
        int part = i % topic->partition_count;
        char key[32], value[96];
        snprintf(key, sizeof(key), "device-%02d", i % 4);
        snprintf(value, sizeof(value),
                 "{\"event\":\"new_data\",\"batch\":2,\"idx\":%d}", i);
        broker_handle_produce(broker, "events", part, key, value, "", &offset);
    }

    for (i = 0; i < topic->partition_count; i++) {
        int64_t committed = offset_fetch(os, "demo-group", "events", i);
        if (committed < 0) committed = 0;
        count = 0;
        broker_handle_fetch(broker, "events", i, committed, 4096, records, &count);
        printf("  partition %d: incremental fetch from %" PRId64 " => %d records\n",
               i, committed, count);
    }

    printf("\n--- Offset Management ---\n");
    printf("  reset partition 0 to earliest\n");
    offset_reset_to_earliest(os, "demo-group", "events", 0);
    printf("  earliest offset: %" PRId64 "\n",
           offset_fetch(os, "demo-group", "events", 0));

    printf("  reset partition 1 to latest\n");
    offset_reset_to_latest(os, "demo-group", "events", 1,
                           partition_get_end_offset(&topic->partitions[1]));
    printf("  latest offset: %" PRId64 "\n",
           offset_fetch(os, "demo-group", "events", 1));

    offset_store_destroy(os);
    broker_destroy(broker);

    printf("\n=== Single Broker Demo Complete ===\n");
    return 0;
}
