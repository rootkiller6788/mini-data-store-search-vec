#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "broker.h"

static int test_broker_create(void) {
    Broker *b = broker_create(1, "localhost", 9092, 1);
    assert(b != NULL);
    assert(b->id == 1);
    assert(b->port == 9092);
    assert(b->is_controller == 1);
    assert(b->topic_count == 0);
    broker_destroy(b);
    return 0;
}

static int test_broker_register_topic(void) {
    Broker *b = broker_create(1, "localhost", 9092, 1);
    Topic *t = topic_create("test-topic", 3, 0);
    assert(t != NULL);

    assert(broker_register_topic(b, t) == 0);
    assert(b->topic_count == 1);
    assert(b->managed_count == 3);

    /* Each partition should have this broker as leader */
    int i;
    for (i = 0; i < 3; i++) {
        assert(t->partitions[i].leader_broker == 1);
    }

    /* NULL safety */
    assert(broker_register_topic(NULL, t) == -1);
    assert(broker_register_topic(b, NULL) == -1);

    broker_destroy(b); /* Also destroys the topic */
    return 0;
}

static int test_broker_produce_fetch(void) {
    Broker *b = broker_create(1, "localhost", 9092, 1);
    Topic *t = topic_create("prod-topic", 2, 0);
    broker_register_topic(b, t);

    int64_t off1, off2;
    assert(broker_handle_produce(b, "prod-topic", 0, "k1", "v1", "", &off1) == 0);
    assert(off1 == 0);

    assert(broker_handle_produce(b, "prod-topic", 0, "k2", "v2", "", &off2) == 0);
    assert(off2 == 1);

    /* Fetch */
    Record records[8];
    int count;
    assert(broker_handle_fetch(b, "prod-topic", 0, 0, 65536, records, &count) == 0);
    assert(count == 2);
    assert(strcmp(records[0].key, "k1") == 0);
    assert(strcmp(records[1].value, "v2") == 0);

    /* Fetch from unknown topic */
    assert(broker_handle_fetch(b, "unknown", 0, 0, 65536, records, &count) == -1);

    /* NULL safety */
    assert(broker_handle_produce(NULL, "t", 0, "k", "v", "", &off1) == -1);

    broker_destroy(b);
    return 0;
}

static int test_broker_metadata(void) {
    Broker *b = broker_create(1, "localhost", 9092, 1);
    Topic *t = topic_create("meta-topic", 4, 0);
    broker_register_topic(b, t);

    int part_count, leader, port;
    assert(broker_handle_metadata(b, "meta-topic", &part_count, &leader, &port) == 0);
    assert(part_count == 4);
    assert(port == 9092);

    /* Unknown topic */
    assert(broker_handle_metadata(b, "unknown", &part_count, &leader, &port) == -1);

    broker_destroy(b);
    return 0;
}

static int test_broker_start(void) {
    Broker *b = broker_create(2, "0.0.0.0", 9093, 0);
    broker_start(b);  /* Exercises the printf path */
    broker_start(NULL);  /* NULL safety */
    broker_destroy(b);
    return 0;
}

int main(void) {
    printf("=== Running Broker Tests ===\n");
    test_broker_create();
    test_broker_register_topic();
    test_broker_produce_fetch();
    test_broker_metadata();
    test_broker_start();
    printf("All broker tests passed!\n");
    return 0;
}