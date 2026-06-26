#ifndef TOPIC_PARTITION_H
#define TOPIC_PARTITION_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PARTITIONS      8
#define MAX_SEGMENTS        8
#define MAX_RECORDS_PER_SEG 128
#define MAX_KEY_LEN         256
#define MAX_VALUE_LEN       4096
#define MAX_HEADER_LEN      1024
#define MAX_TOPIC_NAME_LEN  128
#define MAX_REPLICAS        4

typedef struct {
    int64_t  offset;
    int64_t  timestamp;
    char     key[MAX_KEY_LEN];
    char     value[MAX_VALUE_LEN];
    char     headers[MAX_HEADER_LEN];
} Record;

typedef struct {
    int64_t  base_offset;
    Record   records[MAX_RECORDS_PER_SEG];
    int      size;
    int      is_active;
} LogSegment;

typedef struct {
    int         id;
    int         leader_broker;
    int         replicas[MAX_REPLICAS];
    int         replica_count;
    LogSegment  segments[MAX_SEGMENTS];
    int         segment_count;
    int         active_segment_idx;
} Partition;

typedef struct {
    char        name[MAX_TOPIC_NAME_LEN];
    Partition   partitions[MAX_PARTITIONS];
    int         partition_count;
    int64_t     retention_ms;
} Topic;

Topic*  topic_create(const char *name, int num_partitions, int64_t retention_ms);
void    topic_destroy(Topic *topic);

int64_t partition_append(Partition *p, const char *key, const char *value,
                         const char *headers);
int     partition_read(Partition *p, int64_t start_offset, int max_records,
                       Record *out_records, int *out_count);
void    partition_roll_segment(Partition *p);
int     partition_get_segment_count(const Partition *p);
int64_t partition_get_end_offset(const Partition *p);

#endif
