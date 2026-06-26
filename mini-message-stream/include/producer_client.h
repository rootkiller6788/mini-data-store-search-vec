#ifndef PRODUCER_CLIENT_H
#define PRODUCER_CLIENT_H

#include <stdint.h>

#define MAX_BATCH_SIZE   256
#define MAX_CLIENT_ID_LEN 64

typedef enum {
    PRODUCER_ACK_NONE   = 0,
    PRODUCER_ACK_LEADER = 1,
    PRODUCER_ACK_ALL    = -1
} ProducerAck;

typedef struct {
    char     key[256];
    char     value[4096];
    char     headers[1024];
} BatchRecord;

typedef struct {
    BatchRecord records[MAX_BATCH_SIZE];
    int         batch_size;
} RecordBatch;

typedef struct {
    char        client_id[MAX_CLIENT_ID_LEN];
    char        target_broker[128];
    char        topic[128];
    RecordBatch pending_batch;
    int         linger_ms;
    ProducerAck ack_mode;
    int64_t     next_seq;
} Producer;

Producer* producer_create(const char *client_id, const char *broker,
                          const char *topic, ProducerAck ack, int linger_ms);
void      producer_destroy(Producer *p);
int       producer_send(Producer *p, const char *key, const char *value,
                        const char *headers);
void      producer_flush(Producer *p);
int       producer_partitioner(const char *key, int num_partitions);

#endif
