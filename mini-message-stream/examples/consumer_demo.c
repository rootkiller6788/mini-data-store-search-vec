#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "topic_partition.h"
#include "producer_client.h"
#include "consumer_group.h"
#include "broker.h"
#include "offset_manager.h"

int main(void)
{
    Topic         *topic;
    Broker        *broker;
    ConsumerGroup *group;
    OffsetStore   *os;
    Record         records[32];
    int            i, count, parts[16], part_count, p;

    printf("=== Consumer Group Demo ===\n\n");

    topic = topic_create("logs-topic", 4, 86400000);
    broker = broker_create(1, "localhost", 9092, 1);
    broker_register_topic(broker, topic);
    broker_start(broker);

    printf("\n--- Producing 80 messages to populate topic ---\n");
    for (i = 0; i < 80; i++) {
        char key[32], value[128];
        snprintf(key, sizeof(key), "sensor-%d", i % 6);
        snprintf(value, sizeof(value), "{\"reading\":%d,\"unit\":\"C\"}", 20 + i);
        int part = producer_partitioner(key, topic->partition_count);
        int64_t off = -1;
        broker_handle_produce(broker, "logs-topic", part, key, value, "", &off);
    }
    printf("Produced 80 messages across %d partitions\n", topic->partition_count);

    os = offset_store_create();

    group = group_create("log-consumers", "broker-1:9092", STRATEGY_RANGE);
    printf("\n--- Consumer group '%s' created ---\n", group->group_id);

    printf("\n--- Joining consumers ---\n");
    group_join(group, "consumer-A", 30000);
    group_join(group, "consumer-B", 30000);
    group_join(group, "consumer-C", 30000);

    Topic *tlist[] = { topic };
    group_sync(group, tlist, 1);

    printf("\n--- Heartbeat check ---\n");
    for (i = 0; i < group->member_count; i++) {
        group_heartbeat(group, group->members[i].member_id);
    }
    printf("All heartbeats OK\n");

    printf("\n--- Each consumer reads its assigned partitions ---\n");
    for (i = 0; i < group->member_count; i++) {
        ConsumerMember *m = &group->members[i];

        if (group_get_assignment(group, m->member_id, parts, &part_count) == 0) {
            for (p = 0; p < part_count; p++) {
                int part_id = parts[p];
                int64_t committed = offset_fetch(os, group->group_id,
                                                  "logs-topic", part_id);
                count = 0;
                broker_handle_fetch(broker, "logs-topic", part_id, committed,
                                    65536, records, &count);

                if (count > 0) {
                    int64_t new_offset = records[count - 1].offset + 1;
                    offset_commit(os, group->group_id, "logs-topic",
                                  part_id, new_offset);
                }

                printf("  %s, partition %d: read %d records, committed_offset=%" PRId64,
                       m->member_id, part_id, count,
                       offset_fetch(os, group->group_id, "logs-topic", part_id));

                print_consumer_lag(group->group_id, "logs-topic", part_id,
                                   partition_get_end_offset(&topic->partitions[part_id]),
                                   offset_fetch(os, group->group_id, "logs-topic", part_id));
                printf("\n");
            }
        }
    }

    printf("\n--- Consumer C leaves, triggers rebalance ---\n");
    group_leave(group, "consumer-C");
    group_sync(group, tlist, 1);

    printf("\n--- Resetting offsets for consumer-A, partition 0 ---\n");
    offset_reset_to_earliest(os, group->group_id, "logs-topic", 0);
    printf("Reset: earliest offset = %" PRId64 "\n",
           offset_fetch(os, group->group_id, "logs-topic", 0));

    offset_reset_to_latest(os, group->group_id, "logs-topic", 1,
                           partition_get_end_offset(&topic->partitions[1]));
    printf("Reset: latest offset = %" PRId64 "\n",
           offset_fetch(os, group->group_id, "logs-topic", 1));

    group_destroy(group);
    offset_store_destroy(os);
    broker_destroy(broker);

    printf("\n=== Consumer Group Demo Complete ===\n");
    return 0;
}
