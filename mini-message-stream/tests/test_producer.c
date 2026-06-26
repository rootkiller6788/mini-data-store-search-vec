#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "producer_client.h"

static int test_producer_create(void) {
    Producer *p = producer_create("prod-1", "localhost:9092", "test",
                                   PRODUCER_ACK_LEADER, 100);
    assert(p != NULL);
    assert(strcmp(p->client_id, "prod-1") == 0);
    assert(p->ack_mode == PRODUCER_ACK_LEADER);
    assert(p->linger_ms == 100);
    assert(p->pending_batch.batch_size == 0);
    producer_destroy(p);
    return 0;
}

static int test_producer_send(void) {
    Producer *p = producer_create("p1", "b:9092", "t", PRODUCER_ACK_NONE, 0);
    assert(p != NULL);

    assert(producer_send(p, "key1", "value1", "") == 0);
    assert(p->pending_batch.batch_size == 1);
    assert(p->next_seq == 1);

    assert(producer_send(p, "", "value2", NULL) == 0);
    assert(p->pending_batch.batch_size == 2);

    /* NULL value should fail */
    assert(producer_send(p, "key", NULL, "") == -1);

    producer_destroy(p);
    return 0;
}

static int test_producer_partitioner(void) {
    /* DJB2 hash */
    int p1 = producer_partitioner("hello", 8);
    int p2 = producer_partitioner("hello", 8);
    assert(p1 == p2);
    assert(p1 >= 0 && p1 < 8);
    assert(p2 >= 0 && p2 < 8);

    /* Different keys may go to different partitions */
    int p3 = producer_partitioner("world", 8);
    assert(p3 >= 0 && p3 < 8);

    /* Null key uses time-based round-robin */
    int pn = producer_partitioner(NULL, 4);
    assert(pn >= 0 && pn < 4);

    /* Empty key same as null */
    int pe = producer_partitioner("", 4);
    assert(pe >= 0 && pe < 4);

    /* Invalid partition count */
    assert(producer_partitioner("key", 0) == -1);
    assert(producer_partitioner("key", -1) == -1);

    return 0;
}

static int test_producer_flush(void) {
    Producer *p = producer_create("p1", "b:9092", "t",
                                   PRODUCER_ACK_LEADER, 1000);
    assert(p != NULL);

    /* Flush empty batch */
    producer_flush(p);
    assert(p->pending_batch.batch_size == 0);

    /* Add some records and flush */
    int i;
    for (i = 0; i < 10; i++) {
        char v[64];
        snprintf(v, sizeof(v), "msg-%d", i);
        producer_send(p, "key", v, "");
    }
    assert(p->pending_batch.batch_size == 10);
    producer_flush(p);
    assert(p->pending_batch.batch_size == 0);

    producer_destroy(p);
    return 0;
}

static int test_producer_batch_full_flush(void) {
    Producer *p = producer_create("p1", "b:9092", "t",
                                   PRODUCER_ACK_LEADER, 10000);
    assert(p != NULL);

    /* Fill batch to exactly MAX_BATCH_SIZE */
    int i;
    for (i = 0; i < MAX_BATCH_SIZE; i++) {
        char v[64];
        snprintf(v, sizeof(v), "msg-%d", i);
        int rc = producer_send(p, "k", v, "");
        assert(rc == 0);
    }

    /* Batch should have been auto-flushed */
    assert(p->pending_batch.batch_size < MAX_BATCH_SIZE);

    producer_destroy(p);
    return 0;
}

static int test_producer_null(void) {
    producer_destroy(NULL);
    producer_flush(NULL);
    assert(producer_send(NULL, "k", "v", "") == -1);
    return 0;
}

int main(void) {
    printf("=== Running Producer Tests ===\n");
    test_producer_create();
    test_producer_send();
    test_producer_partitioner();
    test_producer_flush();
    test_producer_batch_full_flush();
    test_producer_null();
    printf("All producer tests passed!\n");
    return 0;
}