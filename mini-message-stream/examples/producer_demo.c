#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "topic_partition.h"
#include "producer_client.h"
#include "broker.h"

int main(void)
{
    Topic    *topic;
    Broker   *broker;
    Producer *producer;
    Record    records[16];
    int       i, count, partition;
    int64_t   offset;
    char      key[32], value[128];

    printf("=== Producer Demo ===\n\n");

    topic = topic_create("orders-topic", 4, 86400000);
    if (!topic) {
        fprintf(stderr, "Failed to create topic\n");
        return 1;
    }
    printf("Created topic '%s' with %d partitions\n", topic->name, topic->partition_count);

    broker = broker_create(1, "localhost", 9092, 1);
    if (!broker) {
        fprintf(stderr, "Failed to create broker\n");
        return 1;
    }
    broker_register_topic(broker, topic);
    broker_start(broker);

    producer = producer_create("producer-001", "localhost:9092", "orders-topic",
                               PRODUCER_ACK_LEADER, 5);
    if (!producer) {
        fprintf(stderr, "Failed to create producer\n");
        return 1;
    }

    printf("\n--- Sending 100 records ---\n");
    srand((unsigned)time(NULL));

    for (i = 0; i < 100; i++) {
        int key_id = i % 5;
        snprintf(key, sizeof(key), "user-%03d", key_id);
        snprintf(value, sizeof(value),
                 "{\"order_id\":%d,\"amount\":%.2f,\"status\":\"new\"}",
                 i, (float)(rand() % 10000) / 100.0f);

        partition = key_id % topic->partition_count;
        offset = -1;

        if (broker_handle_produce(broker, "orders-topic", partition,
                                  key, value, "src=producer-demo",
                                  &offset) == 0) {
            printf("[%03d] produced: partition=%d, offset=%" PRId64
                   ", key=%s\n", i, partition, offset, key);
        } else {
            printf("[%03d] produce failed\n", i);
        }
    }

    producer_flush(producer);

    printf("\n--- Reading records from each partition ---\n");
    for (i = 0; i < topic->partition_count; i++) {
        count = 0;
        broker_handle_fetch(broker, "orders-topic", i, 0, 65536,
                            records, &count);
        printf("Partition %d: %d records, end_offset=%" PRId64 "\n",
               i, count, partition_get_end_offset(&topic->partitions[i]));
    }

    producer_destroy(producer);
    broker_destroy(broker);

    printf("\n=== Producer Demo Complete ===\n");
    return 0;
}
