#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "topic_partition.h"

static int test_topic_create_destroy(void) {
    Topic *t = topic_create("test-topic", 4, 86400000);
    assert(t != NULL);
    assert(strcmp(t->name, "test-topic") == 0);
    assert(t->partition_count == 4);
    assert(t->retention_ms == 86400000);
    topic_destroy(t);

    /* Edge cases */
    assert(topic_create("test", 0, 1000) == NULL);
    assert(topic_create("test", MAX_PARTITIONS + 1, 1000) == NULL);
    return 0;
}

static int test_partition_append(void) {
    Topic *t = topic_create("append-test", 2, 0);
    assert(t != NULL);
    Partition *p = &t->partitions[0];
    assert(p->id == 0);

    int64_t off1 = partition_append(p, "key1", "value1", "hdr1");
    assert(off1 == 0);
    assert(p->segments[0].size == 1);
    assert(strcmp(p->segments[0].records[0].key, "key1") == 0);

    int64_t off2 = partition_append(p, "key2", "value2", "");
    assert(off2 == 1);

    /* NULL value */
    int64_t off3 = partition_append(p, "key3", NULL, NULL);
    assert(off3 == 2);
    assert(p->segments[0].records[2].value[0] == '\0');
    topic_destroy(t);
    return 0;
}

static int test_partition_read(void) {
    Topic *t = topic_create("read-test", 1, 0);
    Partition *p = &t->partitions[0];
    Record records[8];
    int count;
    int i;

    for (i = 0; i < 5; i++) {
        char k[32], v[32];
        snprintf(k, sizeof(k), "key-%d", i);
        snprintf(v, sizeof(v), "value-%d", i);
        partition_append(p, k, v, "");
    }

    /* Read from start */
    memset(records, 0, sizeof(records));
    assert(partition_read(p, 0, 8, records, &count) == 0);
    assert(count == 5);
    assert(records[0].offset == 0);
    assert(records[4].offset == 4);

    /* Read from middle */
    memset(records, 0, sizeof(records));
    assert(partition_read(p, 2, 8, records, &count) == 0);
    assert(count == 3);
    assert(records[0].offset == 2);

    /* Empty read */
    assert(partition_read(p, 100, 8, records, &count) == 0);
    assert(count == 0);

    /* Null safety */
    assert(partition_read(NULL, 0, 8, records, &count) == -1);
    assert(partition_read(p, 0, 8, NULL, NULL) == -1);
    topic_destroy(t);
    return 0;
}

static int test_segment_roll(void) {
    Topic *t = topic_create("roll-test", 1, 0);
    Partition *p = &t->partitions[0];

    assert(p->segment_count == 1);
    assert(p->active_segment_idx == 0);

    /* Fill the segment to force a roll */
    int i;
    for (i = 0; i < MAX_RECORDS_PER_SEG + 10; i++) {
        char k[16], v[16];
        snprintf(k, sizeof(k), "k%d", i);
        snprintf(v, sizeof(v), "v%d", i);
        int64_t off = partition_append(p, k, v, "");
        assert(off == i);
    }

    assert(p->segment_count > 1);
    assert(partition_get_segment_count(p) == p->segment_count);

    /* End offset should be total records */
    assert(partition_get_end_offset(p) == MAX_RECORDS_PER_SEG + 10);
    topic_destroy(t);
    return 0;
}

static int test_partition_edge_cases(void) {
    /* NULL safety */
    assert(partition_append(NULL, "k", "v", "") == -1);
    assert(partition_get_segment_count(NULL) == 0);
    assert(partition_get_end_offset(NULL) == 0);

    /* Zero segments - allocate on heap to avoid stack overflow */
    Partition *p = (Partition*)calloc(1, sizeof(Partition));
    assert(p != NULL);
    p->segment_count = 0;
    assert(partition_append(p, "k", "v", "") == -1);
    free(p);
    return 0;
}

int main(void) {
    printf("=== Running Partition Tests ===\n");
    test_topic_create_destroy();
    test_partition_append();
    test_partition_read();
    test_segment_roll();
    test_partition_edge_cases();
    printf("All topic_partition tests passed!\n");
    return 0;
}