#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "producer_client.h"

Producer* producer_create(const char *client_id, const char *broker,
                          const char *topic, ProducerAck ack, int linger_ms)
{
    Producer *p;

    p = (Producer*)calloc(1, sizeof(Producer));
    if (!p) return NULL;

    strncpy(p->client_id, client_id, MAX_CLIENT_ID_LEN - 1);
    p->client_id[MAX_CLIENT_ID_LEN - 1] = '\0';

    strncpy(p->target_broker, broker, 127);
    p->target_broker[127] = '\0';

    strncpy(p->topic, topic, 127);
    p->topic[127] = '\0';

    p->ack_mode  = ack;
    p->linger_ms = linger_ms;
    p->next_seq  = 0;
    p->pending_batch.batch_size = 0;

    return p;
}

void producer_destroy(Producer *p)
{
    if (p) {
        producer_flush(p);
        free(p);
    }
}

int producer_send(Producer *p, const char *key, const char *value,
                  const char *headers)
{
    RecordBatch *batch;
    BatchRecord *rec;
    int partition;

    if (!p || !value) return -1;

    partition = producer_partitioner(key, MAX_BATCH_SIZE);
    if (partition < 0) return -1;

    batch = &p->pending_batch;

    if (batch->batch_size >= MAX_BATCH_SIZE) {
        producer_flush(p);
    }

    rec = &batch->records[batch->batch_size];

    if (key) {
        strncpy(rec->key, key, 255);
        rec->key[255] = '\0';
    } else {
        rec->key[0] = '\0';
    }

    strncpy(rec->value, value, 4095);
    rec->value[4095] = '\0';

    if (headers) {
        strncpy(rec->headers, headers, 1023);
        rec->headers[1023] = '\0';
    } else {
        rec->headers[0] = '\0';
    }

    batch->batch_size++;
    p->next_seq++;

    if (batch->batch_size >= MAX_BATCH_SIZE) {
        producer_flush(p);
    }

    return 0;
}

void producer_flush(Producer *p)
{
    RecordBatch *batch;
    int i;

    if (!p) return;

    batch = &p->pending_batch;

    if (batch->batch_size == 0) return;

    printf("producer(%s): flushing %d records to broker %s, topic %s, ack=%d\n",
           p->client_id, batch->batch_size, p->target_broker, p->topic,
           (int)p->ack_mode);

    for (i = 0; i < batch->batch_size; i++) {
        BatchRecord *rec = &batch->records[i];
        printf("  [%d] key=%s value=%.48s...\n", i,
               rec->key[0] ? rec->key : "(none)", rec->value);
    }

    batch->batch_size = 0;
}

int producer_partitioner(const char *key, int num_partitions)
{
    unsigned long hash;
    const char *c;

    if (num_partitions <= 0) return -1;

    if (!key || key[0] == '\0') {
        return (int)(time(NULL) % num_partitions);
    }

    hash = 5381;
    for (c = key; *c; c++) {
        hash = ((hash << 5) + hash) + (unsigned char)(*c);
    }

    return (int)(hash % num_partitions);
}
